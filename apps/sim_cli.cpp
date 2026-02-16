#include "f1sim/sim/simulator.hpp"
#include "f1sim/support/replay_logger.hpp"
#include "f1sim/support/scenario_loader.hpp"
#include "f1sim/support/telemetry_seed.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

struct CliArgs {
    std::string scenario = "examples/scenarios/short_race.json";
    std::string telemetry_db = "telemetry.db";
    std::string replay_db = "sim_replay.db";
    int season = 2024;
    int round = 1;
    double tick_seconds = 1.0;
};

bool parse_int(const std::string& s, int* out) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return false;
    *out = static_cast<int>(v);
    return true;
}

bool parse_double(const std::string& s, double* out) {
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') return false;
    *out = v;
    return true;
}

bool parse_args(int argc, char** argv, CliArgs* args) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const std::string& flag) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << flag << "\n";
                return nullptr;
            }
            return argv[++i];
        };
        if (a == "--scenario") {
            const char* v = need(a);
            if (!v) return false;
            args->scenario = v;
        } else if (a == "--telemetry-db") {
            const char* v = need(a);
            if (!v) return false;
            args->telemetry_db = v;
        } else if (a == "--replay-db") {
            const char* v = need(a);
            if (!v) return false;
            args->replay_db = v;
        } else if (a == "--season") {
            const char* v = need(a);
            if (!v || !parse_int(v, &args->season)) return false;
        } else if (a == "--round") {
            const char* v = need(a);
            if (!v || !parse_int(v, &args->round)) return false;
        } else if (a == "--tick") {
            const char* v = need(a);
            if (!v || !parse_double(v, &args->tick_seconds)) return false;
        } else if (a == "--help" || a == "-h") {
            std::cout << "sim_cli [--scenario FILE] [--telemetry-db FILE] [--replay-db FILE]"
                         " [--season N] [--round N] [--tick seconds]\n";
            return false;
        } else {
            std::cerr << "Unknown argument: " << a << "\n";
            return false;
        }
    }
    return true;
}

std::string make_sim_id(int season, int round) {
    return "sim_s" + std::to_string(season) + "_r" + std::to_string(round);
}

} // namespace

int main(int argc, char** argv) {
    CliArgs args;
    if (!parse_args(argc, argv, &args)) return 1;

    f1sim::SimConfig config;
    std::vector<f1sim::DriverProfile> drivers = f1sim::build_demo_grid();

    std::string err;
    if (!f1sim::support::load_scenario_json(args.scenario, &config, &drivers, &err)) {
        std::cerr << "Scenario load failed: " << err << "\n";
        return 1;
    }
    if (!f1sim::support::apply_telemetry_seed(args.telemetry_db, args.season, args.round, &drivers, &err)) {
        std::cerr << "Telemetry seed warning: " << err << "\n";
    }

    f1sim::RaceSimulator sim(config, drivers);
    f1sim::support::ReplayLogger logger;
    if (!logger.open(args.replay_db, make_sim_id(args.season, args.round), &err)) {
        std::cerr << "Replay logger warning: " << err << "\n";
    }

    int frame_idx = 0;
    while (!sim.all_finished()) {
        sim.run_for(args.tick_seconds);
        frame_idx++;
        logger.log_frame(sim, frame_idx, &err);
        logger.log_new_pit_events(sim, &err);

        const auto board = sim.leaderboard();
        std::cout << "\nT+" << static_cast<int>(sim.simulation_time_seconds()) << "s"
                  << " lap " << sim.leader_lap() << "/" << config.total_laps << "\n";
        std::cout << "pos driver            lap   speed(km/h)   tyre   fuel   cmp   pits\n";
        for (std::size_t i = 0; i < board.size(); ++i) {
            const auto& c = board[i];
            std::cout << std::setw(3) << (i + 1) << " "
                      << std::left << std::setw(16) << c.id << std::right << " "
                      << std::setw(4) << c.lap << "   "
                      << std::setw(10) << std::fixed << std::setprecision(1) << (c.speed_mps * 3.6) << "   "
                      << std::setw(4) << std::setprecision(2) << c.tyre << "   "
                      << std::setw(4) << c.fuel << "   "
                      << std::setw(6) << f1sim::to_string(c.compound) << " "
                      << std::setw(4) << c.pit_stops << "\n";
            if (i >= 5) break;
        }

        const auto& pit_events = sim.pit_events();
        if (!pit_events.empty()) {
            const auto& ev = pit_events.back();
            if (ev.sim_time_s >= sim.simulation_time_seconds() - args.tick_seconds - 0.001) {
                std::cout << "pit: " << ev.driver_id << " lap " << ev.lap << " "
                          << f1sim::to_string(ev.from_compound) << "->" << f1sim::to_string(ev.to_compound)
                          << " (" << std::fixed << std::setprecision(2) << ev.stationary_time_s << "s)\n";
            }
        }
    }

    std::cout << "\nFinal classification\n";
    const auto final_board = sim.leaderboard();
    for (std::size_t i = 0; i < final_board.size(); ++i) {
        std::cout << std::setw(2) << (i + 1) << ". " << final_board[i].id << "\n";
    }
    logger.log_frame(sim, frame_idx + 1, &err);
    logger.log_new_pit_events(sim, &err);
    return 0;
}
