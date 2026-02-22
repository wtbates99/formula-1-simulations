#include "f1/sim_core.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace f1 {
namespace {

constexpr float kAirDensity = 1.225f;
constexpr float kGravity = 9.80665f;
constexpr float kMinRpm = 4000.0f;
constexpr float kMaxRpm = 13000.0f;

inline float clampf(float x, float lo, float hi) {
  return std::max(lo, std::min(hi, x));
}

}  // namespace

void CarStateSoA::Resize(std::uint32_t count) {
  s_m.assign(count, 0.0f);
  x_m.assign(count, 0.0f);
  y_m.assign(count, 0.0f);
  yaw_rad.assign(count, 0.0f);
  speed_mps.assign(count, 0.0f);
  accel_long_mps2.assign(count, 0.0f);
  accel_lat_mps2.assign(count, 0.0f);
  engine_rpm.assign(count, kMinRpm);
  lap_time_s.assign(count, 0.0f);
  last_lap_time_s.assign(count, 0.0f);
  gear.assign(count, 1);
  lap.assign(count, 0);
}

SimulationCore::SimulationCore(const SimConfig& sim_cfg, const CarConfig& car_cfg,
                               const TrackConfig& track_cfg)
    : sim_cfg_(sim_cfg), car_cfg_(car_cfg) {
  track_.Load(track_cfg);
  SetCarCount(std::min(sim_cfg_.max_cars, 1u));
  replay_frames_.reserve(sim_cfg_.replay_capacity_steps);
}

void SimulationCore::SetCarCount(std::uint32_t count) {
  car_count_ = std::min(count, sim_cfg_.max_cars);
  state_.Resize(car_count_);
}

void SimulationCore::Reset() {
  state_.Resize(car_count_);
  replay_frames_.clear();
}

float SimulationCore::engine_torque_nm(float rpm) const {
  if (car_cfg_.powertrain.torque_curve == nullptr || car_cfg_.powertrain.torque_curve_count == 0) {
    return 0.0f;
  }

  const TorquePoint* c = car_cfg_.powertrain.torque_curve;
  const std::uint32_t n = car_cfg_.powertrain.torque_curve_count;

  if (rpm <= c[0].rpm) {
    return c[0].torque_nm;
  }
  if (rpm >= c[n - 1].rpm) {
    return c[n - 1].torque_nm;
  }

  for (std::uint32_t i = 1; i < n; ++i) {
    if (rpm <= c[i].rpm) {
      const float r0 = c[i - 1].rpm;
      const float r1 = c[i].rpm;
      const float t = (rpm - r0) / (r1 - r0);
      return c[i - 1].torque_nm + (c[i].torque_nm - c[i - 1].torque_nm) * t;
    }
  }
  return c[n - 1].torque_nm;
}

void SimulationCore::auto_shift(std::uint32_t i) {
  if (car_cfg_.powertrain.gear_count < 2) {
    return;
  }
  auto& g = state_.gear[i];
  const float rpm = state_.engine_rpm[i];
  if (rpm > car_cfg_.powertrain.shift_rpm_up && g < car_cfg_.powertrain.gear_count) {
    ++g;
  } else if (rpm < car_cfg_.powertrain.shift_rpm_down && g > 1) {
    --g;
  }
}

void SimulationCore::Step(const DriverInput* inputs, std::uint32_t input_count) {
  const float dt = sim_cfg_.fixed_dt;

  if (capture_replay_ && replay_frames_.size() < sim_cfg_.replay_capacity_steps) {
    ReplayFrame frame{};
    frame.inputs.resize(car_count_);
    for (std::uint32_t i = 0; i < car_count_; ++i) {
      frame.inputs[i] = (i < input_count) ? inputs[i] : DriverInput{0.0f, 0.0f, 0.0f};
    }
    replay_frames_.push_back(std::move(frame));
  }

  for (std::uint32_t i = 0; i < car_count_; ++i) {
    const DriverInput in = (i < input_count) ? inputs[i] : DriverInput{0.0f, 0.0f, 0.0f};

    const float throttle = clampf(in.throttle, 0.0f, 1.0f);
    const float brake = clampf(in.brake, 0.0f, 1.0f);
    const float steer = clampf(in.steer, -1.0f, 1.0f);

    const float v = std::max(0.0f, state_.speed_mps[i]);
    const float curv_track = track_.curvature(state_.s_m[i]);

    auto_shift(i);

    const std::uint32_t gear_idx = std::max(1u, std::min(state_.gear[i], car_cfg_.powertrain.gear_count)) - 1;
    const float ratio = car_cfg_.powertrain.gear_ratios[gear_idx] * car_cfg_.powertrain.final_drive;

    const float wheel_omega = v / std::max(0.05f, car_cfg_.tire_radius_m);
    const float engine_rpm = clampf(wheel_omega * ratio * 60.0f / (2.0f * static_cast<float>(M_PI)),
                                    kMinRpm, kMaxRpm);
    state_.engine_rpm[i] = engine_rpm;

    const float engine_torque = engine_torque_nm(engine_rpm) * throttle;
    const float drive_torque = engine_torque * ratio * car_cfg_.powertrain.driveline_efficiency;
    const float f_drive = drive_torque / std::max(0.05f, car_cfg_.tire_radius_m);

    const float downforce = 0.5f * kAirDensity * car_cfg_.clA * v * v;
    const float normal = car_cfg_.mass_kg * kGravity + downforce;

    const float f_long_max = car_cfg_.mu_long * normal;
    const float f_drive_limited = std::min(f_drive, f_long_max);
    const float f_brake = brake * car_cfg_.brake_force_max_n;
    const float f_drag = 0.5f * kAirDensity * car_cfg_.cdA * v * v;

    const float f_net_long = f_drive_limited - f_brake - car_cfg_.rolling_resistance - f_drag;
    const float a_long = f_net_long / car_cfg_.mass_kg;

    const float curv_cmd = curv_track + steer * car_cfg_.steer_gain / std::max(1.0f, car_cfg_.wheelbase_m);
    const float a_lat_unclamped = v * v * curv_cmd;
    const float a_lat_max = car_cfg_.mu_lat * normal / car_cfg_.mass_kg;
    const float a_lat = clampf(a_lat_unclamped, -a_lat_max, a_lat_max);

    // Speed scrub when lateral limit is exceeded keeps the system stable near grip limit.
    const float lat_saturation = std::fabs(a_lat_unclamped) > 1e-3f
                                     ? std::min(1.0f, std::fabs(a_lat) / std::fabs(a_lat_unclamped))
                                     : 1.0f;
    const float a_scrub = (1.0f - lat_saturation) * 4.0f;

    float v_next = std::max(0.0f, v + (a_long - a_scrub) * dt);

    const float yaw_rate = (v_next > 0.1f) ? (a_lat / v_next) : 0.0f;
    state_.yaw_rad[i] += yaw_rate * dt;

    state_.x_m[i] += std::cos(state_.yaw_rad[i]) * v_next * dt;
    state_.y_m[i] += std::sin(state_.yaw_rad[i]) * v_next * dt;

    state_.s_m[i] += v_next * dt;
    while (state_.s_m[i] >= track_.length()) {
      state_.s_m[i] -= track_.length();
      state_.last_lap_time_s[i] = state_.lap_time_s[i];
      state_.lap_time_s[i] = 0.0f;
      ++state_.lap[i];
    }

    state_.lap_time_s[i] += dt;
    state_.speed_mps[i] = v_next;
    state_.accel_long_mps2[i] = a_long;
    state_.accel_lat_mps2[i] = a_lat;
  }
}

void SimulationCore::StartReplayCapture() {
  capture_replay_ = true;
  replay_frames_.clear();
}

void SimulationCore::StopReplayCapture() { capture_replay_ = false; }

bool SimulationCore::ReplayCapturedDeterministic() {
  if (replay_frames_.empty()) {
    return false;
  }

  const std::vector<ReplayFrame> recorded = replay_frames_;
  const CarStateSoA baseline = state_;

  Reset();
  for (const auto& f : recorded) {
    Step(f.inputs.data(), static_cast<std::uint32_t>(f.inputs.size()));
  }

  bool deterministic = true;
  for (std::uint32_t i = 0; i < car_count_; ++i) {
    deterministic = deterministic &&
                    (std::fabs(state_.speed_mps[i] - baseline.speed_mps[i]) < 1e-5f) &&
                    (std::fabs(state_.s_m[i] - baseline.s_m[i]) < 1e-4f) &&
                    (state_.lap[i] == baseline.lap[i]);
  }

  return deterministic;
}

BatchLapResult SimulationCore::RunBatchLaps(std::uint32_t car_index, std::uint32_t laps) {
  BatchLapResult out{};
  if (car_index >= car_count_ || laps == 0) {
    return out;
  }

  Reset();

  float total = 0.0f;
  float best = std::numeric_limits<float>::max();
  std::uint32_t done = 0;

  DriverInput in{};
  while (done < laps) {
    const float curv = std::fabs(track_.curvature(state_.s_m[car_index]));
    in.throttle = (curv < 0.02f) ? 1.0f : 0.6f;
    in.brake = (curv > 0.05f && state_.speed_mps[car_index] > 72.0f) ? 0.55f : 0.0f;
    in.steer = clampf(track_.curvature(state_.s_m[car_index]) * 60.0f, -1.0f, 1.0f);

    Step(&in, 1);

    if (state_.lap[car_index] > done) {
      const float lap_time = state_.last_lap_time_s[car_index];
      total += lap_time;
      best = std::min(best, lap_time);
      ++done;
    }
  }

  out.laps_completed = done;
  out.mean_lap_time_s = (done > 0) ? total / static_cast<float>(done) : 0.0f;
  out.best_lap_time_s = (done > 0) ? best : 0.0f;
  return out;
}

void SimulationCore::Snapshot(std::uint32_t car_index, CarSnapshot* out) const {
  if (out == nullptr || car_index >= car_count_) {
    return;
  }

  out->s_m = state_.s_m[car_index];
  out->x_m = state_.x_m[car_index];
  out->y_m = state_.y_m[car_index];
  out->yaw_rad = state_.yaw_rad[car_index];
  out->speed_mps = state_.speed_mps[car_index];
  out->accel_long_mps2 = state_.accel_long_mps2[car_index];
  out->accel_lat_mps2 = state_.accel_lat_mps2[car_index];
  out->engine_rpm = state_.engine_rpm[car_index];
  out->gear = state_.gear[car_index];
  out->lap = state_.lap[car_index];
  out->lap_time_s = state_.lap_time_s[car_index];
  out->last_lap_time_s = state_.last_lap_time_s[car_index];
}

}  // namespace f1
