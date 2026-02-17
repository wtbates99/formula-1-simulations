# F1 Simulations (C++)

This repo is structured for two goals:

1. Ingest high-detail telemetry from Jolpica/Ergast into SQLite.
2. Run and visualize C++ race simulations you can expand into a full F1 sim stack.

## Project layout

```text
.
├── apps/
│   ├── sim_cli.cpp          # terminal simulation example
│   └── sim_viewer.cpp       # raylib graphics viewer example
├── include/
│   └── f1sim/
│       ├── sim/simulator.hpp           # simulation interfaces and race model
│       └── support/                    # scenario load, telemetry seed, replay logging
├── src/
│   ├── sim/simulator.cpp               # core race loop logic
│   └── support/                        # shared app support modules
├── examples/
│   ├── scenarios/short_race.json       # scenario input template
│   └── sql/leaderboard_query.sql       # telemetry query starter
├── CMakeLists.txt
└── main.cpp                 # interactive CLI entrypoint target: f1_cli
```

## Telemetry tables

`f1_cli` telemetry ingest menu writes detailed line-item tables:

- `telemetry_lap_timings`
  - `(season, round, lap, driver_id, position, lap_time, lap_time_ms)`
- `telemetry_pit_stops`
  - `(season, round, driver_id, stop, lap, pit_time_hms, duration, duration_ms)`

Both tables use primary keys plus upserts, so re-runs are safe.

Simulation replay logging writes:

- `sim_replay_frames`: one row per car per frame snapshot
- `sim_replay_pit_events`: pit event stream with tyre-compound transitions

## Quick start

Build:

```bash
cmake -S . -B build-cmake
cmake --build build-cmake -j
```

Run the interactive CLI:

```bash
./build-cmake/f1_cli
```

From the CLI menu, choose:
- single-race ingest
- full historical ingest (year range + all rounds)
- text simulation (scenario + telemetry seeding + replay logging)
- quick telemetry row counts

## Current simulation features

- Scenario JSON loading from `examples/scenarios/*.json`
- Telemetry-driven driver parameter seeding from `telemetry_lap_timings`
- Tyre compound model (`soft`, `medium`, `hard`) affecting pace and wear
- Pit strategy model (planned laps + tyre-degradation fallback)
- Replay logging to SQLite for frame and pit-event analysis

## Example expansion path

- Add weather and track evolution impacts on tyre wear and pace.
- Add setup dimensions (drag/downforce) and overtaking model.
- Add safety-car / VSC periods and incident probabilities.
- Use replay tables as training labels for strategy optimization.

## CMake targets

- `f1_cli`: interactive entry point for ingest + text simulation + DB checks.
- `sim_viewer`: real-time visualization if raylib is installed.
