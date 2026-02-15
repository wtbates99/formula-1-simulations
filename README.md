# F1 Historical SQLite Warehouse

This project ingests historical Formula 1 data from the Ergast/Jolpica API and stores it in a local SQLite database you can read from C++.

## What gets stored

- Seasons and race calendar metadata
- Circuits
- Drivers
- Constructors
- Race results
- Qualifying results
- Sprint results
- Driver standings after each round
- Constructor standings after each round
- Lap-by-lap timing data (default on)
- Pit stop data (default on)
- ML-oriented feature view and materialized feature table

## Build the database

Run from project root (lap times + pit stops included by default):

```bash
python main.py --db f1_history.db --from-year 1950 --to-year 2025
```

Skip large tables if needed:

```bash
python main.py --db f1_history.db --from-year 1950 --to-year 2025 --skip-lap-times --skip-pit-stops
```

Notes:
- Lap data can make the DB very large and ingestion slower.
- You can re-run safely; tables use upserts.
- `driver_race_features` is rebuilt by default after ingest; skip with `--skip-feature-rebuild`.

## Training features

- `v_driver_race_features`: query-time feature view.
- `driver_race_features`: materialized snapshot table for faster C++ training loops.

Feature columns include:
- race context: season, round, grid, constructor, standing positions/points
- performance: finish position, points, laps, race time
- pace/strategy: avg lap time, best lap time, pit stop count, pit total duration
- rolling form: trailing 3-race and 5-race averages (finish, points)
- target labels: finish position, points, podium flag, win flag