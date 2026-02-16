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
├── scripts/
│   ├── setup_dev.sh         # apt packages for development
│   ├── build.sh             # cmake build helper
│   ├── run_cli_sim.sh       # run CLI simulation
│   ├── run_viewer.sh        # run graphics viewer
│   └── ingest_telemetry.sh  # ingest telemetry into SQLite
├── CMakeLists.txt
└── main.cpp                 # telemetry ingester target: telemetry_ingest
```

## Telemetry tables

`telemetry_ingest` writes detailed line-item tables:

- `telemetry_lap_timings`
  - `(season, round, lap, driver_id, position, lap_time, lap_time_ms)`
- `telemetry_pit_stops`
  - `(season, round, driver_id, stop, lap, pit_time_hms, duration, duration_ms)`

Both tables use primary keys plus upserts, so re-runs are safe.

Simulation replay logging writes:

- `sim_replay_frames`: one row per car per frame snapshot
- `sim_replay_pit_events`: pit event stream with tyre-compound transitions

## Quick start

Install dependencies:

```bash
./scripts/setup_dev.sh
```

Build:

```bash
./scripts/build.sh
```

Ingest telemetry for one race:

```bash
./scripts/ingest_telemetry.sh build-cmake 2024 1 telemetry.db 1000
```

Run simulation in terminal (loads scenario + telemetry seeding + replay logging):

```bash
./scripts/run_cli_sim.sh build-cmake --scenario examples/scenarios/short_race.json --telemetry-db telemetry.db --replay-db sim_replay.db --season 2024 --round 1 --tick 1.0
```

Run graphics viewer (requires `libraylib-dev`):

```bash
./scripts/run_viewer.sh build-cmake --scenario examples/scenarios/short_race.json --telemetry-db telemetry.db --replay-db sim_replay.db --season 2024 --round 1
```

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

- `telemetry_ingest`: pulls race telemetry and stores into SQLite.
- `sim_cli`: simple race simulation in terminal.
- `sim_viewer`: real-time visualization if raylib is installed.
