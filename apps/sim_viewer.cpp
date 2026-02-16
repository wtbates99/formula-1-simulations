#include "f1sim/sim/simulator.hpp"
#include "f1sim/support/replay_logger.hpp"
#include "f1sim/support/scenario_loader.hpp"
#include "f1sim/support/telemetry_seed.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "raylib.h"

namespace {
Color palette(int idx) {
    static Color colors[] = {
        RED, ORANGE, GOLD, GREEN, SKYBLUE, BLUE, PURPLE, PINK, BROWN, MAROON,
    };
    return colors[idx % (sizeof(colors) / sizeof(colors[0]))];
}

struct ViewerArgs {
    std::string scenario = "examples/scenarios/short_race.json";
    std::string telemetry_db = "telemetry.db";
    std::string replay_db = "sim_replay.db";
    int season = 2024;
    int round = 1;
};

bool parse_int(const std::string& s, int* out) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return false;
    *out = static_cast<int>(v);
    return true;
}

bool parse_args(int argc, char** argv, ViewerArgs* out) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need = [&](const std::string& flag) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << flag << "\n";
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--scenario") {
            const char* v = need(arg);
            if (!v) return false;
            out->scenario = v;
        } else if (arg == "--telemetry-db") {
            const char* v = need(arg);
            if (!v) return false;
            out->telemetry_db = v;
        } else if (arg == "--replay-db") {
            const char* v = need(arg);
            if (!v) return false;
            out->replay_db = v;
        } else if (arg == "--season") {
            const char* v = need(arg);
            if (!v || !parse_int(v, &out->season)) return false;
        } else if (arg == "--round") {
            const char* v = need(arg);
            if (!v || !parse_int(v, &out->round)) return false;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "sim_viewer [--scenario FILE] [--telemetry-db FILE] [--replay-db FILE] [--season N] [--round N]\n";
            return false;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return false;
        }
    }
    return true;
}

std::string make_sim_id(int season, int round) {
    return "viewer_s" + std::to_string(season) + "_r" + std::to_string(round);
}
} // namespace

int main(int argc, char** argv) {
    ViewerArgs args;
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
    config.dt_seconds = 1.0 / 120.0;

    f1sim::RaceSimulator sim(config, drivers);
    f1sim::support::ReplayLogger logger;
    if (!logger.open(args.replay_db, make_sim_id(args.season, args.round), &err)) {
        std::cerr << "Replay logger warning: " << err << "\n";
    }
    int frame_idx = 0;

    const int width = 1360;
    const int height = 840;
    InitWindow(width, height, "F1 Simulation Viewer");
    SetTargetFPS(60);

    const Vector2 center = {width * 0.48f, height * 0.52f};
    const float rx = 430.0f;
    const float ry = 250.0f;
    std::size_t rendered_pit_events = 0;

    while (!WindowShouldClose()) {
        sim.run_for(GetFrameTime() * 5.0);
        frame_idx++;
        logger.log_frame(sim, frame_idx, &err);
        logger.log_new_pit_events(sim, &err);

        BeginDrawing();
        ClearBackground((Color){18, 22, 28, 255});

        DrawEllipseLines(static_cast<int>(center.x), static_cast<int>(center.y), rx, ry, LIGHTGRAY);
        DrawEllipseLines(static_cast<int>(center.x), static_cast<int>(center.y), rx - 40.0f, ry - 40.0f, DARKGRAY);

        const auto& cars = sim.cars();
        for (std::size_t i = 0; i < cars.size(); ++i) {
            const auto& c = cars[i];
            const double t = std::clamp(c.distance_on_lap_m / config.track_length_m, 0.0, 1.0);
            const double angle = (t * 2.0 * PI) - PI / 2.0;
            const float x = center.x + static_cast<float>(std::cos(angle) * (rx - 20.0f));
            const float y = center.y + static_cast<float>(std::sin(angle) * (ry - 20.0f));

            DrawCircleV({x, y}, 8.0f, palette(static_cast<int>(i)));
        }

        DrawText("Live Leaderboard", 980, 50, 24, RAYWHITE);
        const auto board = sim.leaderboard();
        for (std::size_t i = 0; i < board.size() && i < 10; ++i) {
            const auto& c = board[i];
            const std::string line =
                std::to_string(i + 1) + ". " + c.id + "  L" + std::to_string(c.lap) + "  " + f1sim::to_string(c.compound) +
                "  P" + std::to_string(c.pit_stops) + "  " + std::to_string(static_cast<int>(c.speed_mps * 3.6)) + " km/h";
            DrawText(line.c_str(), 980, 90 + static_cast<int>(i) * 26, 20, RAYWHITE);
        }

        const std::string title = "Sim time: " + std::to_string(static_cast<int>(sim.simulation_time_seconds())) +
                                  "s   Leader lap: " + std::to_string(sim.leader_lap()) + "/" +
                                  std::to_string(config.total_laps);
        DrawText(title.c_str(), 40, 36, 24, RAYWHITE);

        const auto& pit_events = sim.pit_events();
        DrawText("Pit events", 980, 390, 22, YELLOW);
        const int show_count = 8;
        const int start = static_cast<int>(pit_events.size() > static_cast<std::size_t>(show_count)
                                               ? pit_events.size() - static_cast<std::size_t>(show_count)
                                               : 0);
        for (int i = start; i < static_cast<int>(pit_events.size()); ++i) {
            const auto& ev = pit_events[static_cast<std::size_t>(i)];
            const std::string line = ev.driver_id + " L" + std::to_string(ev.lap) + " " +
                                     f1sim::to_string(ev.from_compound) + "->" + f1sim::to_string(ev.to_compound) + " " +
                                     std::to_string(static_cast<int>(ev.stationary_time_s * 1000.0)) + "ms";
            const int y = 420 + ((i - start) * 24);
            DrawText(line.c_str(), 980, y, 18, ORANGE);
        }

        if (pit_events.size() > rendered_pit_events) {
            rendered_pit_events = pit_events.size();
        }

        if (sim.all_finished()) {
            DrawText("RACE FINISHED", 520, 780, 28, YELLOW);
        }

        EndDrawing();
    }

    CloseWindow();
    logger.log_frame(sim, frame_idx + 1, &err);
    logger.log_new_pit_events(sim, &err);
    return 0;
}
