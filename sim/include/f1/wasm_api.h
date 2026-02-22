#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float s;
  float curvature;
  float elevation;
} wasm_track_node_t;

typedef struct {
  float rpm;
  float torque_nm;
} wasm_torque_point_t;

typedef struct {
  float fixed_dt;
  uint32_t max_cars;
  uint32_t replay_capacity_steps;
  uint32_t active_cars;

  float track_length_m;
  const wasm_track_node_t* track_nodes;
  uint32_t track_node_count;

  float mass_kg;
  float wheelbase_m;
  float cg_to_front_m;
  float cg_to_rear_m;
  float tire_radius_m;
  float mu_long;
  float mu_lat;
  float cdA;
  float clA;
  float rolling_resistance;
  float brake_force_max_n;
  float steer_gain;

  float gear_ratios[8];
  uint32_t gear_count;
  float final_drive;
  float driveline_efficiency;
  float shift_rpm_up;
  float shift_rpm_down;

  const wasm_torque_point_t* torque_curve;
  uint32_t torque_curve_count;
} wasm_init_config_t;

typedef struct {
  float s_m;
  float x_m;
  float y_m;
  float yaw_rad;
  float speed_mps;
  float accel_long_mps2;
  float accel_lat_mps2;
  float engine_rpm;
  uint32_t gear;
  uint32_t lap;
  float lap_time_s;
  float last_lap_time_s;
} wasm_vehicle_state_t;

int32_t init_sim(const wasm_init_config_t* cfg);
void reset_sim(void);
void set_controls(float throttle, float brake, float steering);
void step_sim(float dt);
void get_vehicle_state(uint32_t car_index, wasm_vehicle_state_t* out_state);
float run_lap(void);

const float* state_x_ptr(void);
const float* state_y_ptr(void);
const float* state_yaw_ptr(void);
const float* state_speed_ptr(void);
uint32_t state_car_count(void);

#ifdef __cplusplus
}
#endif
