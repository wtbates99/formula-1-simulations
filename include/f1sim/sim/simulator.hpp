#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace f1sim {

enum class TyreCompound {
    Soft,
    Medium,
    Hard,
};

std::string to_string(TyreCompound compound);
TyreCompound tyre_compound_from_string(const std::string& value);

struct DriverProfile {
    std::string id;
    std::string team;
    double skill = 0.5;      // 0..1
    double aggression = 0.5; // 0..1
    double consistency = 0.6;
    TyreCompound start_compound = TyreCompound::Medium;
    std::vector<int> planned_pit_laps;
};

struct SimConfig {
    double track_length_m = 5412.0;
    int total_laps = 57;
    double dt_seconds = 1.0 / 60.0;
    std::uint32_t seed = 42;
};

struct CarState {
    std::string id;
    std::string team;
    double skill = 0.5;
    double aggression = 0.5;
    double consistency = 0.6;

    double speed_mps = 70.0;
    double distance_total_m = 0.0;
    double distance_on_lap_m = 0.0;
    int lap = 1;
    bool finished = false;

    double tyre = 1.0; // 1.0 fresh, 0.0 dead
    double fuel = 1.0; // 1.0 full, 0.0 empty
    TyreCompound compound = TyreCompound::Medium;
    int pit_stops = 0;
    bool in_pit = false;
    double pit_time_remaining_s = 0.0;
    int last_pit_lap = -1;
    std::vector<int> planned_pit_laps;
};

struct PitEvent {
    double sim_time_s = 0.0;
    std::string driver_id;
    int lap = 0;
    TyreCompound from_compound = TyreCompound::Medium;
    TyreCompound to_compound = TyreCompound::Medium;
    double stationary_time_s = 0.0;
};

class RaceSimulator {
public:
    RaceSimulator(const SimConfig& config, std::vector<DriverProfile> drivers);

    void step(double dt_seconds);
    void step_default();
    void run_for(double seconds);

    bool all_finished() const;
    int leader_lap() const;
    std::vector<CarState> leaderboard() const;
    const std::vector<CarState>& cars() const;
    const SimConfig& config() const;
    double simulation_time_seconds() const;
    const std::vector<PitEvent>& pit_events() const;

private:
    SimConfig config_;
    std::vector<CarState> cars_;
    std::vector<PitEvent> pit_events_;
    double simulation_time_seconds_ = 0.0;
    std::uint32_t rng_state_ = 0;

    double random_unit();
    bool should_pit(const CarState& car) const;
    void start_pit_stop(CarState* car);
};

std::vector<DriverProfile> build_demo_grid();

} // namespace f1sim
