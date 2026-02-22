#include "f1/c_api.h"

#include <algorithm>
#include <array>
#include <memory>

#include "f1/sim_core.hpp"

namespace {

struct Handle {
  std::vector<f1::TrackNode> track_nodes;
  std::vector<f1::TorquePoint> torque_points;
  std::unique_ptr<f1::SimulationCore> core;
};

constexpr std::array<f1sim_track_node_t, 16> kDefaultTrackNodes = {{
    {0.0f, 0.000f, 0.0f},   {350.0f, 0.000f, 0.0f}, {620.0f, 0.018f, 0.5f},
    {810.0f, 0.040f, 1.0f}, {980.0f, 0.008f, 1.5f}, {1220.0f, -0.010f, 1.2f},
    {1600.0f, -0.024f, 0.8f}, {1880.0f, -0.006f, 0.3f}, {2250.0f, 0.000f, -0.2f},
    {2600.0f, 0.022f, -0.5f}, {2820.0f, 0.048f, -0.8f}, {3000.0f, 0.005f, -1.0f},
    {3400.0f, -0.010f, -0.6f}, {3800.0f, -0.030f, -0.1f}, {4150.0f, -0.004f, 0.2f},
    {4500.0f, 0.000f, 0.0f},
}};

constexpr std::array<f1sim_torque_point_t, 7> kDefaultTorqueCurve = {{
    {4000.0f, 510.0f},
    {6000.0f, 640.0f},
    {8000.0f, 760.0f},
    {9500.0f, 810.0f},
    {11000.0f, 780.0f},
    {12000.0f, 730.0f},
    {13000.0f, 640.0f},
}};

f1::PowertrainConfig to_cpp(const f1sim_powertrain_config_t& c, std::vector<f1::TorquePoint>& torque_points) {
  f1::PowertrainConfig out{};
  for (std::size_t i = 0; i < out.gear_ratios.size(); ++i) {
    out.gear_ratios[i] = c.gear_ratios[i];
  }
  out.gear_count = c.gear_count;
  out.final_drive = c.final_drive;
  out.driveline_efficiency = c.driveline_efficiency;
  out.shift_rpm_up = c.shift_rpm_up;
  out.shift_rpm_down = c.shift_rpm_down;

  torque_points.resize(c.torque_curve_count);
  for (uint32_t i = 0; i < c.torque_curve_count; ++i) {
    torque_points[i] = f1::TorquePoint{c.torque_curve[i].rpm, c.torque_curve[i].torque_nm};
  }
  out.torque_curve = torque_points.data();
  out.torque_curve_count = static_cast<uint32_t>(torque_points.size());
  return out;
}

f1::CarConfig to_cpp(const f1sim_car_config_t& c, std::vector<f1::TorquePoint>& torque_points) {
  f1::CarConfig out{};
  out.mass_kg = c.mass_kg;
  out.wheelbase_m = c.wheelbase_m;
  out.cg_to_front_m = c.cg_to_front_m;
  out.cg_to_rear_m = c.cg_to_rear_m;
  out.tire_radius_m = c.tire_radius_m;
  out.mu_long = c.mu_long;
  out.mu_lat = c.mu_lat;
  out.cdA = c.cdA;
  out.clA = c.clA;
  out.rolling_resistance = c.rolling_resistance;
  out.brake_force_max_n = c.brake_force_max_n;
  out.steer_gain = c.steer_gain;
  out.powertrain = to_cpp(c.powertrain, torque_points);
  return out;
}

f1::SimConfig to_cpp(const f1sim_sim_config_t& c) {
  f1::SimConfig out{};
  out.fixed_dt = c.fixed_dt;
  out.max_cars = c.max_cars;
  out.replay_capacity_steps = c.replay_capacity_steps;
  return out;
}

f1::TrackConfig to_cpp(const f1sim_track_config_t& c, std::vector<f1::TrackNode>& track_nodes) {
  track_nodes.resize(c.node_count);
  for (uint32_t i = 0; i < c.node_count; ++i) {
    track_nodes[i] = f1::TrackNode{c.nodes[i].s, c.nodes[i].curvature, c.nodes[i].elevation};
  }
  return f1::TrackConfig{track_nodes.data(), static_cast<uint32_t>(track_nodes.size()), c.length_m};
}

Handle* from_handle(f1sim_handle_t h) { return reinterpret_cast<Handle*>(h); }
const Handle* from_handle_const(f1sim_handle_t h) { return reinterpret_cast<const Handle*>(h); }

}  // namespace

extern "C" {

uint32_t f1sim_api_version(void) { return F1SIM_API_VERSION; }

void f1sim_default_track_config(f1sim_track_config_t* out_cfg) {
  if (out_cfg == nullptr) {
    return;
  }
  out_cfg->nodes = kDefaultTrackNodes.data();
  out_cfg->node_count = static_cast<uint32_t>(kDefaultTrackNodes.size());
  out_cfg->length_m = 4600.0f;
}

void f1sim_default_car_config(f1sim_car_config_t* out_cfg) {
  if (out_cfg == nullptr) {
    return;
  }
  *out_cfg = {};
  out_cfg->mass_kg = 798.0f;
  out_cfg->wheelbase_m = 3.6f;
  out_cfg->cg_to_front_m = 1.6f;
  out_cfg->cg_to_rear_m = 2.0f;
  out_cfg->tire_radius_m = 0.34f;
  out_cfg->mu_long = 1.85f;
  out_cfg->mu_lat = 2.1f;
  out_cfg->cdA = 1.12f;
  out_cfg->clA = 3.2f;
  out_cfg->rolling_resistance = 180.0f;
  out_cfg->brake_force_max_n = 18500.0f;
  out_cfg->steer_gain = 0.22f;

  out_cfg->powertrain.gear_ratios[0] = 3.18f;
  out_cfg->powertrain.gear_ratios[1] = 2.31f;
  out_cfg->powertrain.gear_ratios[2] = 1.79f;
  out_cfg->powertrain.gear_ratios[3] = 1.45f;
  out_cfg->powertrain.gear_ratios[4] = 1.22f;
  out_cfg->powertrain.gear_ratios[5] = 1.05f;
  out_cfg->powertrain.gear_ratios[6] = 0.92f;
  out_cfg->powertrain.gear_ratios[7] = 0.82f;
  out_cfg->powertrain.gear_count = 8;
  out_cfg->powertrain.final_drive = 3.05f;
  out_cfg->powertrain.driveline_efficiency = 0.92f;
  out_cfg->powertrain.shift_rpm_up = 11800.0f;
  out_cfg->powertrain.shift_rpm_down = 6200.0f;
  out_cfg->powertrain.torque_curve = kDefaultTorqueCurve.data();
  out_cfg->powertrain.torque_curve_count = static_cast<uint32_t>(kDefaultTorqueCurve.size());
}

void f1sim_default_sim_config(f1sim_sim_config_t* out_cfg) {
  if (out_cfg == nullptr) {
    return;
  }
  out_cfg->fixed_dt = 1.0f / 240.0f;
  out_cfg->max_cars = 20;
  out_cfg->replay_capacity_steps = 120000;
}

f1sim_handle_t f1sim_create(const f1sim_sim_config_t* sim_cfg, const f1sim_car_config_t* car_cfg,
                            const f1sim_track_config_t* track_cfg) {
  if (sim_cfg == nullptr || car_cfg == nullptr || track_cfg == nullptr) {
    return nullptr;
  }

  auto handle = std::make_unique<Handle>();
  const f1::SimConfig sim = to_cpp(*sim_cfg);
  const f1::CarConfig car = to_cpp(*car_cfg, handle->torque_points);
  const f1::TrackConfig track = to_cpp(*track_cfg, handle->track_nodes);

  handle->core = std::make_unique<f1::SimulationCore>(sim, car, track);
  handle->core->SetCarCount(std::min(sim.max_cars, 1u));
  return reinterpret_cast<f1sim_handle_t>(handle.release());
}

void f1sim_destroy(f1sim_handle_t handle) {
  delete from_handle(handle);
}

void f1sim_set_car_count(f1sim_handle_t handle, uint32_t count) {
  if (auto* h = from_handle(handle); h != nullptr) {
    h->core->SetCarCount(count);
  }
}

void f1sim_reset(f1sim_handle_t handle) {
  if (auto* h = from_handle(handle); h != nullptr) {
    h->core->Reset();
  }
}

void f1sim_step(f1sim_handle_t handle, const f1sim_driver_input_t* inputs, uint32_t input_count) {
  if (auto* h = from_handle(handle); h != nullptr) {
    h->core->Step(reinterpret_cast<const f1::DriverInput*>(inputs), input_count);
  }
}

void f1sim_start_replay_capture(f1sim_handle_t handle) {
  if (auto* h = from_handle(handle); h != nullptr) {
    h->core->StartReplayCapture();
  }
}

void f1sim_stop_replay_capture(f1sim_handle_t handle) {
  if (auto* h = from_handle(handle); h != nullptr) {
    h->core->StopReplayCapture();
  }
}

int32_t f1sim_replay_captured_deterministic(f1sim_handle_t handle) {
  if (auto* h = from_handle(handle); h != nullptr) {
    return h->core->ReplayCapturedDeterministic() ? 1 : 0;
  }
  return 0;
}

f1sim_batch_lap_result_t f1sim_run_batch_laps(f1sim_handle_t handle, uint32_t car_index, uint32_t laps) {
  f1sim_batch_lap_result_t out{};
  if (auto* h = from_handle(handle); h != nullptr) {
    const f1::BatchLapResult r = h->core->RunBatchLaps(car_index, laps);
    out.mean_lap_time_s = r.mean_lap_time_s;
    out.best_lap_time_s = r.best_lap_time_s;
    out.laps_completed = r.laps_completed;
  }
  return out;
}

void f1sim_snapshot(f1sim_handle_t handle, uint32_t car_index, f1sim_car_snapshot_t* out_snapshot) {
  if (auto* h = from_handle(handle); h != nullptr && out_snapshot != nullptr) {
    f1::CarSnapshot tmp{};
    h->core->Snapshot(car_index, &tmp);
    out_snapshot->s_m = tmp.s_m;
    out_snapshot->x_m = tmp.x_m;
    out_snapshot->y_m = tmp.y_m;
    out_snapshot->yaw_rad = tmp.yaw_rad;
    out_snapshot->speed_mps = tmp.speed_mps;
    out_snapshot->accel_long_mps2 = tmp.accel_long_mps2;
    out_snapshot->accel_lat_mps2 = tmp.accel_lat_mps2;
    out_snapshot->engine_rpm = tmp.engine_rpm;
    out_snapshot->gear = tmp.gear;
    out_snapshot->lap = tmp.lap;
    out_snapshot->lap_time_s = tmp.lap_time_s;
    out_snapshot->last_lap_time_s = tmp.last_lap_time_s;
  }
}

const float* f1sim_state_speed_ptr(f1sim_handle_t handle) {
  if (auto* h = from_handle_const(handle); h != nullptr) {
    return h->core->state().speed_mps.data();
  }
  return nullptr;
}

const float* f1sim_state_x_ptr(f1sim_handle_t handle) {
  if (auto* h = from_handle_const(handle); h != nullptr) {
    return h->core->state().x_m.data();
  }
  return nullptr;
}

const float* f1sim_state_y_ptr(f1sim_handle_t handle) {
  if (auto* h = from_handle_const(handle); h != nullptr) {
    return h->core->state().y_m.data();
  }
  return nullptr;
}

const float* f1sim_state_yaw_ptr(f1sim_handle_t handle) {
  if (auto* h = from_handle_const(handle); h != nullptr) {
    return h->core->state().yaw_rad.data();
  }
  return nullptr;
}

const float* f1sim_state_s_ptr(f1sim_handle_t handle) {
  if (auto* h = from_handle_const(handle); h != nullptr) {
    return h->core->state().s_m.data();
  }
  return nullptr;
}

uint32_t f1sim_car_count(f1sim_handle_t handle) {
  if (auto* h = from_handle_const(handle); h != nullptr) {
    return h->core->car_count();
  }
  return 0;
}

}  // extern "C"
