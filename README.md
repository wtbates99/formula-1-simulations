# Formula 1 Simulations

This repo has two main parts:

1. FastF1 data ingestion into SQLite for analysis (`main.py`).
2. A deterministic C++20 simulation core compiled for native CLI and WebAssembly, with a TypeScript/WebGL frontend.

## Repository Layout

- `main.py`: downloads and normalizes FastF1 data into SQLite tables.
- `sim/`: C++ physics core, track model, C API, and wasm-facing API.
- `ts/`: Vite + TypeScript frontend and WebGL renderer.
- `config/`: simulation, car, and track JSON configs used by the frontend.
- `scripts/`: helper scripts for native and wasm builds.
- `emsdk/`: vendored Emscripten SDK used for wasm builds.

## Prerequisites

- Python 3.10+
- `uv` (recommended) or `pip`
- CMake 3.21+
- A C++20 compiler (clang++ or g++)
- Node.js 18+
- `npm`
- Emscripten for wasm builds (already vendored in `emsdk/`)

## Python Data Ingestion

Install dependencies:

```bash
uv sync
```

Run ingestion (example: 2025 round 3 race session):

```bash
uv run python main.py --year 2025 --round 3 --session R
```

Ingest all session types (FP1/FP2/FP3/Q/R):

```bash
uv run python main.py --year 2025 --round 3 --all-sessions
```

Ingest the full 2025 season (all rounds, all sessions, all drivers with available telemetry):

```bash
uv run python main.py --year 2025 --all-sessions
```

Output database defaults to `f1.sqlite` and can be changed with `--db-path`.

## Build Native CLI

```bash
./scripts/build_native.sh
./build-native/sim/f1sim_cli
```

## Build WebAssembly Module

```bash
./scripts/build_wasm.sh
```

Wasm artifacts are copied to:

- `ts/public/wasm/f1sim.js`
- `ts/public/wasm/f1sim.wasm`

## Run Web App

Start the SQLite-backed config API (Terminal 1):

```bash
./scripts/run_data_api.sh
```

Start frontend dev server (Terminal 2):

```bash
cd ts
npm install
npm run dev
```

Then open `http://localhost:5173`.

Optional URL params to choose dataset:

- `year` (e.g. `2025`)
- `round` (e.g. `3`)
- `session` (`FP1|FP2|FP3|Q|R`)
- `driver` (code or number, e.g. `VER` or `1`)

Example:

`http://localhost:5173/?year=2025&round=3&session=R&driver=VER`

In-app workflow:

1. `Mode = Replay Actual Race` to watch selected real drivers from SQLite telemetry.
2. `Mode = Simulate My Driver vs Actual` to run C++/Wasm simulation with scenario controls.
3. Use `Run Lap` to compare simulated lap time against actual fastest lap from your selected race/session.

## Simulation Notes

- Deterministic fixed-step integration (`fixed_dt`), no randomness in core loop.
- Structure-of-arrays state representation for car data.
- Exported wasm API in `sim/include/f1/wasm_api.h`.
- Frontend reads simulation state through typed views over wasm memory.
