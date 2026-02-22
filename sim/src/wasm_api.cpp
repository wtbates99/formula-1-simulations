#include "f1/wasm_api.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "f1/sim_core.hpp"

namespace {

struct Runtime {
  std::vector<f1::TrackNode> track_nodes;
  std::vector<f1::TorquePoint> torque_points;
  std::unique_ptr<f1::SimulationCore> core;
  std::vector<f1::DriverInput> inputs;
  float dt_remainder_s = 0.0f;
};

std::unique_ptr<Runtime> g_rt;

float clampf(float x, float lo, float hi) {
  return std::max(lo, std::min(hi, x));
}

void update_ai_inputs(Runtime& rt) {
  const std::uint32_t cars = rt.core->car_count();
  if (rt.inputs.size() != cars) {
    rt.inputs.resize(cars, f1::DriverInput{0.0f, 0.0f, 0.0f});
  }

  const auto& s = rt.core->state();
  for (std::uint32_t i = 1; i < cars; ++i) {
    const float phase = static_cast<float>(i) * 0.35f + s.s_m[i] * 0.003f;
    const float throttle = 0.70f + 0.22f * std::sin(phase);
    rt.inputs[i].throttle = clampf(throttle, 0.0f, 1.0f);
    rt.inputs[i].brake = (s.speed_mps[i] > 83.0f) ? 0.2f : 0.0f;
    rt.inputs[i].steer = 0.16f * std::sin(phase * 0.8f);
  }
}

}  // namespace

extern "C" {

int32_t init_sim(const wasm_init_config_t* cfg) {
  if (cfg == nullptr || cfg->track_nodes == nullptr || cfg->torque_curve == nullptr ||
      cfg->track_node_count < 2 || cfg->torque_curve_count < 2) {
    return 0;
  }

  auto rt = std::make_unique<Runtime>();
  rt->track_nodes.resize(cfg->track_node_count);
  for (uint32_t i = 0; i < cfg->track_node_count; ++i) {
    rt->track_nodes[i] = f1::TrackNode{cfg->track_nodes[i].s, cfg->track_nodes[i].curvature,
                                       cfg->track_nodes[i].elevation};
  }

  rt->torque_points.resize(cfg->torque_curve_count);
  for (uint32_t i = 0; i < cfg->torque_curve_count; ++i) {
    rt->torque_points[i] =
        f1::TorquePoint{cfg->torque_curve[i].rpm, cfg->torque_curve[i].torque_nm};
  }

  f1::TrackConfig track_cfg{rt->track_nodes.data(), static_cast<uint32_t>(rt->track_nodes.size()),
                            cfg->track_length_m};

  f1::CarConfig car_cfg{};
  car_cfg.mass_kg = cfg->mass_kg;
  car_cfg.wheelbase_m = cfg->wheelbase_m;
  car_cfg.cg_to_front_m = cfg->cg_to_front_m;
  car_cfg.cg_to_rear_m = cfg->cg_to_rear_m;
  car_cfg.tire_radius_m = cfg->tire_radius_m;
  car_cfg.mu_long = cfg->mu_long;
  car_cfg.mu_lat = cfg->mu_lat;
  car_cfg.cdA = cfg->cdA;
  car_cfg.clA = cfg->clA;
  car_cfg.rolling_resistance = cfg->rolling_resistance;
  car_cfg.brake_force_max_n = cfg->brake_force_max_n;
  car_cfg.steer_gain = cfg->steer_gain;
  car_cfg.powertrain.gear_count = std::min(cfg->gear_count, 8u);
  for (std::size_t i = 0; i < car_cfg.powertrain.gear_ratios.size(); ++i) {
    car_cfg.powertrain.gear_ratios[i] = cfg->gear_ratios[i];
  }
  car_cfg.powertrain.final_drive = cfg->final_drive;
  car_cfg.powertrain.driveline_efficiency = cfg->driveline_efficiency;
  car_cfg.powertrain.shift_rpm_up = cfg->shift_rpm_up;
  car_cfg.powertrain.shift_rpm_down = cfg->shift_rpm_down;
  car_cfg.powertrain.torque_curve = rt->torque_points.data();
  car_cfg.powertrain.torque_curve_count = static_cast<uint32_t>(rt->torque_points.size());

  f1::SimConfig sim_cfg{};
  sim_cfg.fixed_dt = cfg->fixed_dt;
  sim_cfg.max_cars = cfg->max_cars;
  sim_cfg.replay_capacity_steps = cfg->replay_capacity_steps;

  rt->core = std::make_unique<f1::SimulationCore>(sim_cfg, car_cfg, track_cfg);
  rt->core->SetCarCount(std::min(cfg->active_cars, cfg->max_cars));
  rt->inputs.resize(rt->core->car_count(), f1::DriverInput{0.0f, 0.0f, 0.0f});
  rt->core->StartReplayCapture();

  g_rt = std::move(rt);
  return 1;
}

void reset_sim(void) {
  if (!g_rt || !g_rt->core) {
    return;
  }
  g_rt->core->Reset();
  g_rt->dt_remainder_s = 0.0f;
}

void set_controls(float throttle, float brake, float steering) {
  if (!g_rt || !g_rt->core || g_rt->inputs.empty()) {
    return;
  }
  g_rt->inputs[0].throttle = clampf(throttle, 0.0f, 1.0f);
  g_rt->inputs[0].brake = clampf(brake, 0.0f, 1.0f);
  g_rt->inputs[0].steer = clampf(steering, -1.0f, 1.0f);
}

void step_sim(float dt) {
  if (!g_rt || !g_rt->core) {
    return;
  }

  const float fixed_dt = g_rt->core->dt();
  g_rt->dt_remainder_s += std::max(0.0f, dt);
  update_ai_inputs(*g_rt);

  int max_steps = 8192;
  while (g_rt->dt_remainder_s >= fixed_dt && max_steps-- > 0) {
    g_rt->dt_remainder_s -= fixed_dt;
    g_rt->core->Step(g_rt->inputs.data(), static_cast<uint32_t>(g_rt->inputs.size()));
  }
}

void get_vehicle_state(uint32_t car_index, wasm_vehicle_state_t* out_state) {
  if (!g_rt || !g_rt->core || out_state == nullptr || car_index >= g_rt->core->car_count()) {
    return;
  }

  f1::CarSnapshot snap{};
  g_rt->core->Snapshot(car_index, &snap);

  out_state->s_m = snap.s_m;
  out_state->x_m = snap.x_m;
  out_state->y_m = snap.y_m;
  out_state->yaw_rad = snap.yaw_rad;
  out_state->speed_mps = snap.speed_mps;
  out_state->accel_long_mps2 = snap.accel_long_mps2;
  out_state->accel_lat_mps2 = snap.accel_lat_mps2;
  out_state->engine_rpm = snap.engine_rpm;
  out_state->gear = snap.gear;
  out_state->lap = snap.lap;
  out_state->lap_time_s = snap.lap_time_s;
  out_state->last_lap_time_s = snap.last_lap_time_s;
}

float run_lap(void) {
  if (!g_rt || !g_rt->core || g_rt->core->car_count() == 0) {
    return 0.0f;
  }
  const f1::BatchLapResult r = g_rt->core->RunBatchLaps(0, 1);
  return r.best_lap_time_s;
}

const float* state_x_ptr(void) {
  return (g_rt && g_rt->core) ? g_rt->core->state().x_m.data() : nullptr;
}

const float* state_y_ptr(void) {
  return (g_rt && g_rt->core) ? g_rt->core->state().y_m.data() : nullptr;
}

const float* state_yaw_ptr(void) {
  return (g_rt && g_rt->core) ? g_rt->core->state().yaw_rad.data() : nullptr;
}

const float* state_speed_ptr(void) {
  return (g_rt && g_rt->core) ? g_rt->core->state().speed_mps.data() : nullptr;
}

uint32_t state_car_count(void) {
  return (g_rt && g_rt->core) ? g_rt->core->car_count() : 0;
}

}  // extern "C"
