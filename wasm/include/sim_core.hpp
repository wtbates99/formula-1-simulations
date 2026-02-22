#pragma once

#include <cstdint>
#include <vector>

#include "f1/sim_types.hpp"
#include "f1/track.hpp"

namespace f1 {

struct CarStateSoA {
  std::vector<float> s_m;
  std::vector<float> x_m;
  std::vector<float> y_m;
  std::vector<float> yaw_rad;
  std::vector<float> speed_mps;
  std::vector<float> accel_long_mps2;
  std::vector<float> accel_lat_mps2;
  std::vector<float> engine_rpm;
  std::vector<float> lap_time_s;
  std::vector<float> last_lap_time_s;
  std::vector<std::uint32_t> gear;
  std::vector<std::uint32_t> lap;

  void Resize(std::uint32_t count);
};

class SimulationCore {
 public:
  SimulationCore(const SimConfig& sim_cfg, const CarConfig& car_cfg, const TrackConfig& track_cfg);

  std::uint32_t car_count() const { return car_count_; }
  float dt() const { return sim_cfg_.fixed_dt; }

  void SetCarCount(std::uint32_t count);
  void Reset();
  void Step(const DriverInput* inputs, std::uint32_t input_count);

  void StartReplayCapture();
  void StopReplayCapture();
  bool ReplayCapturedDeterministic();

  BatchLapResult RunBatchLaps(std::uint32_t car_index, std::uint32_t laps);

  void Snapshot(std::uint32_t car_index, CarSnapshot* out) const;

  const CarStateSoA& state() const { return state_; }

 private:
  struct ReplayFrame {
    std::vector<DriverInput> inputs;
  };

  float engine_torque_nm(float rpm) const;
  void auto_shift(std::uint32_t i);

  SimConfig sim_cfg_{};
  CarConfig car_cfg_{};
  TrackProfile track_{};
  CarStateSoA state_{};

  std::uint32_t car_count_ = 0;
  bool capture_replay_ = false;
  std::vector<ReplayFrame> replay_frames_;
};

}  // namespace f1
