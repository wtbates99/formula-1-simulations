#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define F1SIM_API_VERSION 1u

typedef struct {
  float s;
  float curvature;
  float elevation;
} f1sim_track_node_t;

typedef struct {
  const f1sim_track_node_t* nodes;
  uint32_t node_count;
  float length_m;
} f1sim_track_config_t;

typedef struct {
  float rpm;
  float torque_nm;
} f1sim_torque_point_t;

typedef struct {
  float gear_ratios[8];
  uint32_t gear_count;
  float final_drive;
  float driveline_efficiency;
  float shift_rpm_up;
  float shift_rpm_down;
  const f1sim_torque_point_t* torque_curve;
  uint32_t torque_curve_count;
} f1sim_powertrain_config_t;

typedef struct {
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
  f1sim_powertrain_config_t powertrain;
} f1sim_car_config_t;

typedef struct {
  float fixed_dt;
  uint32_t max_cars;
  uint32_t replay_capacity_steps;
} f1sim_sim_config_t;

typedef struct {
  float throttle;
  float brake;
  float steer;
} f1sim_driver_input_t;

typedef struct {
  float mean_lap_time_s;
  float best_lap_time_s;
  uint32_t laps_completed;
} f1sim_batch_lap_result_t;

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
} f1sim_car_snapshot_t;

typedef void* f1sim_handle_t;

uint32_t f1sim_api_version(void);

void f1sim_default_track_config(f1sim_track_config_t* out_cfg);
void f1sim_default_car_config(f1sim_car_config_t* out_cfg);
void f1sim_default_sim_config(f1sim_sim_config_t* out_cfg);

f1sim_handle_t f1sim_create(const f1sim_sim_config_t* sim_cfg,
                            const f1sim_car_config_t* car_cfg,
                            const f1sim_track_config_t* track_cfg);
void f1sim_destroy(f1sim_handle_t handle);

void f1sim_set_car_count(f1sim_handle_t handle, uint32_t count);
void f1sim_reset(f1sim_handle_t handle);
void f1sim_step(f1sim_handle_t handle, const f1sim_driver_input_t* inputs, uint32_t input_count);

void f1sim_start_replay_capture(f1sim_handle_t handle);
void f1sim_stop_replay_capture(f1sim_handle_t handle);
int32_t f1sim_replay_captured_deterministic(f1sim_handle_t handle);

f1sim_batch_lap_result_t f1sim_run_batch_laps(f1sim_handle_t handle, uint32_t car_index, uint32_t laps);
void f1sim_snapshot(f1sim_handle_t handle, uint32_t car_index, f1sim_car_snapshot_t* out_snapshot);

const float* f1sim_state_speed_ptr(f1sim_handle_t handle);
const float* f1sim_state_x_ptr(f1sim_handle_t handle);
const float* f1sim_state_y_ptr(f1sim_handle_t handle);
const float* f1sim_state_yaw_ptr(f1sim_handle_t handle);
const float* f1sim_state_s_ptr(f1sim_handle_t handle);
uint32_t f1sim_car_count(f1sim_handle_t handle);

#ifdef __cplusplus
}
#endif
