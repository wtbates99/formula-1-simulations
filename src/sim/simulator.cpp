#include "f1sim/sim/simulator.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace f1sim {

namespace {
constexpr double kBaseRacePaceMps = 78.0;
constexpr double kMaxBoostMps = 11.0;
constexpr double kNoiseMps = 1.8;

double compound_pace_delta(TyreCompound compound) {
    switch (compound) {
        case TyreCompound::Soft: return 2.2;
        case TyreCompound::Medium: return 0.0;
        case TyreCompound::Hard: return -0.9;
    }
    return 0.0;
}

double compound_wear_multiplier(TyreCompound compound) {
    switch (compound) {
        case TyreCompound::Soft: return 1.55;
        case TyreCompound::Medium: return 1.0;
        case TyreCompound::Hard: return 0.72;
    }
    return 1.0;
}

TyreCompound next_compound(TyreCompound current) {
    switch (current) {
        case TyreCompound::Soft: return TyreCompound::Hard;
        case TyreCompound::Medium: return TyreCompound::Hard;
        case TyreCompound::Hard: return TyreCompound::Medium;
    }
    return TyreCompound::Medium;
}
} // namespace

std::string to_string(TyreCompound compound) {
    switch (compound) {
        case TyreCompound::Soft: return "soft";
        case TyreCompound::Medium: return "medium";
        case TyreCompound::Hard: return "hard";
    }
    return "medium";
}

TyreCompound tyre_compound_from_string(const std::string& value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (char c : value) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lowered == "soft" || lowered == "s") return TyreCompound::Soft;
    if (lowered == "hard" || lowered == "h") return TyreCompound::Hard;
    return TyreCompound::Medium;
}

RaceSimulator::RaceSimulator(const SimConfig& config, std::vector<DriverProfile> drivers)
    : config_(config), rng_state_(config.seed) {
    cars_.reserve(drivers.size());
    for (const auto& d : drivers) {
        CarState c;
        c.id = d.id;
        c.team = d.team;
        c.skill = std::clamp(d.skill, 0.0, 1.0);
        c.aggression = std::clamp(d.aggression, 0.0, 1.0);
        c.consistency = std::clamp(d.consistency, 0.0, 1.0);
        c.speed_mps = kBaseRacePaceMps;
        c.compound = d.start_compound;
        c.planned_pit_laps = d.planned_pit_laps;
        cars_.push_back(c);
    }
}

double RaceSimulator::random_unit() {
    // Xorshift32: tiny deterministic RNG good enough for toy simulation.
    rng_state_ ^= rng_state_ << 13;
    rng_state_ ^= rng_state_ >> 17;
    rng_state_ ^= rng_state_ << 5;
    const double normalized = static_cast<double>(rng_state_ & 0x00FFFFFFu) / static_cast<double>(0x01000000u);
    return normalized;
}

bool RaceSimulator::should_pit(const CarState& car) const {
    if (car.in_pit || car.finished) return false;
    if (car.lap >= config_.total_laps - 1) return false;
    if (car.last_pit_lap == car.lap) return false;

    for (int planned_lap : car.planned_pit_laps) {
        if (car.lap == planned_lap) return true;
    }

    const bool tyre_trigger = car.tyre < (0.20 + (0.08 * (1.0 - car.aggression)));
    if (tyre_trigger && car.pit_stops < 3) return true;
    return false;
}

void RaceSimulator::start_pit_stop(CarState* car) {
    const TyreCompound from = car->compound;
    const TyreCompound to = next_compound(from);
    const double stationary = 2.2 + (1.3 * random_unit()) + ((1.0 - car->consistency) * 0.8);

    car->in_pit = true;
    car->pit_time_remaining_s = stationary;
    car->pit_stops += 1;
    car->last_pit_lap = car->lap;
    car->compound = to;
    car->tyre = 1.0;

    PitEvent ev;
    ev.sim_time_s = simulation_time_seconds_;
    ev.driver_id = car->id;
    ev.lap = car->lap;
    ev.from_compound = from;
    ev.to_compound = to;
    ev.stationary_time_s = stationary;
    pit_events_.push_back(ev);
}

void RaceSimulator::step(double dt_seconds) {
    simulation_time_seconds_ += dt_seconds;
    const double race_distance = config_.track_length_m * static_cast<double>(config_.total_laps);

    for (auto& car : cars_) {
        if (car.finished) continue;

        if (car.in_pit) {
            car.pit_time_remaining_s = std::max(0.0, car.pit_time_remaining_s - dt_seconds);
            car.speed_mps = 0.0;
            if (car.pit_time_remaining_s <= 0.0) car.in_pit = false;
            continue;
        }

        if (should_pit(car)) {
            start_pit_stop(&car);
            continue;
        }

        const double performance = (car.skill * 0.65) + (car.aggression * 0.35);
        const double tyre_factor = 0.80 + (0.20 * car.tyre);
        const double fuel_factor = 0.88 + (0.12 * (1.0 - car.fuel));
        const double consistency_noise_scale = 1.0 - (0.65 * car.consistency);
        const double noise = (random_unit() - 0.5) * 2.0 * kNoiseMps * consistency_noise_scale;
        const double compound_delta = compound_pace_delta(car.compound);

        const double target_speed = kBaseRacePaceMps + (performance * kMaxBoostMps) + compound_delta;
        car.speed_mps = std::max(20.0, (target_speed * tyre_factor * fuel_factor) + noise);

        const double dist_step = car.speed_mps * dt_seconds;
        car.distance_total_m += dist_step;
        car.distance_on_lap_m = std::fmod(car.distance_total_m, config_.track_length_m);
        car.lap = static_cast<int>(car.distance_total_m / config_.track_length_m) + 1;

        const double wear_step = (0.000022 + (0.00002 * car.aggression)) * compound_wear_multiplier(car.compound);
        car.tyre = std::max(0.12, car.tyre - wear_step);
        const double fuel_step = 0.000018;
        car.fuel = std::max(0.0, car.fuel - fuel_step);

        if (car.distance_total_m >= race_distance) {
            car.finished = true;
            car.distance_total_m = race_distance;
            car.distance_on_lap_m = config_.track_length_m;
            car.lap = config_.total_laps;
        }
    }
}

void RaceSimulator::step_default() {
    step(config_.dt_seconds);
}

void RaceSimulator::run_for(double seconds) {
    if (seconds <= 0.0) return;
    const int steps = std::max(1, static_cast<int>(seconds / config_.dt_seconds));
    for (int i = 0; i < steps; ++i) {
        step_default();
    }
}

bool RaceSimulator::all_finished() const {
    for (const auto& c : cars_) {
        if (!c.finished) return false;
    }
    return true;
}

int RaceSimulator::leader_lap() const {
    int max_lap = 0;
    for (const auto& c : cars_) max_lap = std::max(max_lap, c.lap);
    return max_lap;
}

std::vector<CarState> RaceSimulator::leaderboard() const {
    std::vector<CarState> board = cars_;
    std::sort(board.begin(), board.end(), [](const CarState& a, const CarState& b) {
        return a.distance_total_m > b.distance_total_m;
    });
    return board;
}

const std::vector<CarState>& RaceSimulator::cars() const {
    return cars_;
}

const SimConfig& RaceSimulator::config() const {
    return config_;
}

double RaceSimulator::simulation_time_seconds() const {
    return simulation_time_seconds_;
}

const std::vector<PitEvent>& RaceSimulator::pit_events() const {
    return pit_events_;
}

std::vector<DriverProfile> build_demo_grid() {
    return {
        {"max_verstappen", "Red Bull", 0.98, 0.92, 0.92, TyreCompound::Soft, {15, 38}},
        {"perez", "Red Bull", 0.85, 0.72, 0.80, TyreCompound::Medium, {18, 41}},
        {"leclerc", "Ferrari", 0.92, 0.82, 0.87, TyreCompound::Soft, {16, 40}},
        {"sainz", "Ferrari", 0.89, 0.74, 0.84, TyreCompound::Medium, {20, 42}},
        {"hamilton", "Mercedes", 0.92, 0.70, 0.90, TyreCompound::Medium, {19, 43}},
        {"russell", "Mercedes", 0.88, 0.76, 0.83, TyreCompound::Soft, {17, 39}},
        {"norris", "McLaren", 0.90, 0.83, 0.86, TyreCompound::Soft, {16, 37}},
        {"piastri", "McLaren", 0.86, 0.72, 0.82, TyreCompound::Medium, {20, 44}},
        {"alonso", "Aston Martin", 0.91, 0.80, 0.89, TyreCompound::Soft, {18, 41}},
        {"stroll", "Aston Martin", 0.79, 0.63, 0.76, TyreCompound::Hard, {24}},
    };
}

} // namespace f1sim
