#pragma once

#include <array>
#include <cstdint>

namespace f1 {

constexpr std::uint32_t kApiVersion = 1;
constexpr std::size_t kMaxGears = 8;

struct TrackNode {
  float s;
  float curvature;
  float elevation;
};

struct TrackConfig {
  const TrackNode* nodes;
  std::uint32_t node_count;
  float length_m;
};

struct TorquePoint {
  float rpm;
  float torque_nm;
};

struct PowertrainConfig {
  std::array<float, kMaxGears> gear_ratios{};
  std::uint32_t gear_count = 0;
  float final_drive = 3.0f;
  float driveline_efficiency = 0.92f;
  float shift_rpm_up = 11500.0f;
  float shift_rpm_down = 6000.0f;
  const TorquePoint* torque_curve = nullptr;
  std::uint32_t torque_curve_count = 0;
};

struct CarConfig {
  float mass_kg = 798.0f;
  float wheelbase_m = 3.6f;
  float cg_to_front_m = 1.6f;
  float cg_to_rear_m = 2.0f;
  float tire_radius_m = 0.34f;
  float mu_long = 1.85f;
  float mu_lat = 2.1f;
  float cdA = 1.12f;
  float clA = 3.2f;
  float rolling_resistance = 180.0f;
  float brake_force_max_n = 18500.0f;
  float steer_gain = 0.22f;
  PowertrainConfig powertrain{};
};

struct SimConfig {
  float fixed_dt = 1.0f / 240.0f;
  std::uint32_t max_cars = 20;
  std::uint32_t replay_capacity_steps = 120000;
};

struct DriverInput {
  float throttle;
  float brake;
  float steer;
};

struct CarSnapshot {
  float s_m;
  float x_m;
  float y_m;
  float yaw_rad;
  float speed_mps;
  float accel_long_mps2;
  float accel_lat_mps2;
  float engine_rpm;
  std::uint32_t gear;
  std::uint32_t lap;
  float lap_time_s;
  float last_lap_time_s;
};

struct BatchLapResult {
  float mean_lap_time_s;
  float best_lap_time_s;
  std::uint32_t laps_completed;
};

}  // namespace f1
