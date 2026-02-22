#include <array>
#include <cstdio>

#include "f1/c_api.h"

int main() {
  f1sim_sim_config_t sim_cfg{};
  f1sim_car_config_t car_cfg{};
  f1sim_track_config_t track_cfg{};

  f1sim_default_sim_config(&sim_cfg);
  f1sim_default_car_config(&car_cfg);
  f1sim_default_track_config(&track_cfg);

  f1sim_handle_t sim = f1sim_create(&sim_cfg, &car_cfg, &track_cfg);
  if (!sim) {
    std::fprintf(stderr, "failed to create simulator\n");
    return 1;
  }

  f1sim_set_car_count(sim, 1);
  f1sim_driver_input_t in{1.0f, 0.0f, 0.0f};

  for (int i = 0; i < 240 * 90; ++i) {
    f1sim_car_snapshot_t snap{};
    f1sim_snapshot(sim, 0, &snap);

    const float steer_from_curvature = (snap.speed_mps > 40.0f) ? 0.1f : 0.0f;
    in.steer = steer_from_curvature;
    in.brake = (snap.speed_mps > 85.0f) ? 0.3f : 0.0f;

    f1sim_step(sim, &in, 1);
  }

  const f1sim_batch_lap_result_t batch = f1sim_run_batch_laps(sim, 0, 5);
  std::printf("batch mean lap: %.3fs best: %.3fs laps: %u\n", batch.mean_lap_time_s,
              batch.best_lap_time_s, batch.laps_completed);

  f1sim_destroy(sim);
  return 0;
}
