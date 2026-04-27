# Formula 1 Simulations

Formula 1 Simulations is a data and visualization workspace for FastF1 telemetry
experiments. It ingests Formula 1 session data into DuckDB, supports SQL-based
analysis through tabletalk context files, and includes browser-based replay
experiments for exploring race telemetry.

## Capabilities

- Downloads FastF1 event schedules, session results, laps, weather, status
  messages, race control messages, car telemetry, and position telemetry.
- Writes normalized tables to a DuckDB database.
- Supports single-session, full-round, and full-season ingestion.
- Provides a Plotly-based race replay visualization for the local DuckDB file.
- Includes a Svelte/Tauri replay experiment in `f1-replay`.

## Requirements

- Python 3.10+
- `uv`
- Network access for FastF1 data downloads
- Node.js and npm for the `f1-replay` frontend experiment

## Install

```bash
git clone https://github.com/wtbates99/formula-1-simulations.git
cd formula-1-simulations
uv sync
```

## Ingest Data

Ingest one race session:

```bash
uv run python main.py --year 2025 --round 3 --session R
```

Ingest every session type for a round:

```bash
uv run python main.py --year 2025 --round 3 --all-sessions
```

Ingest every configured session type for a season:

```bash
uv run python main.py --year 2025 --all-sessions
```

The default database path is `f1.duckdb`. Override it with `--db-path`.

## Visualize

After ingesting compatible session data, generate the Plotly replay:

```bash
uv run python visual.py
```

The script reads from `f1.duckdb` and writes an HTML visualization.

## Query with tabletalk

This repo includes a `tabletalk.yaml` and context file for SQL-assisted
exploration of the generated DuckDB database.

```bash
uv run tabletalk apply
uv run tabletalk query
```

## Frontend Experiment

```bash
cd f1-replay
npm install
npm run dev
```

For the desktop shell:

```bash
npm run tauri dev
```

## Notes

FastF1 data availability depends on the upstream API and session support. Keep
analysis scripts defensive around missing telemetry, partial sessions, and
schema differences between race weekends.
