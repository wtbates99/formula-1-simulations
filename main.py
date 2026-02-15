import argparse
import datetime as dt
import json
import sqlite3
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from email.utils import parsedate_to_datetime
from pathlib import Path
from typing import Any

API_BASE = "https://api.jolpi.ca/ergast/f1"


@dataclass(frozen=True)
class SyncConfig:
    db_path: Path
    from_year: int
    to_year: int
    from_round: int | None
    to_round: int | None
    sleep_seconds: float
    max_retries: int
    include_lap_times: bool
    include_pit_stops: bool
    rebuild_features: bool


class JolpicaClient:
    def __init__(self, sleep_seconds: float = 0.2, max_retries: int = 5):
        self.sleep_seconds = sleep_seconds
        self.max_retries = max_retries
        self._last_request_ts = 0.0

    def fetch_all(self, endpoint: str, page_size: int = 1000) -> dict[str, Any]:
        offset = 0
        merged_payload: dict[str, Any] | None = None
        key_path: tuple[str, ...] | None = None
        while True:
            payload = self._request(endpoint, limit=page_size, offset=offset)
            mr_data = payload["MRData"]
            if merged_payload is None:
                merged_payload = payload
                key_path = self._locate_list_path(mr_data)
            else:
                assert key_path is not None
                self._dig(merged_payload["MRData"], key_path).extend(self._dig(mr_data, key_path))
            total = int(mr_data.get("total", "0"))
            limit = int(mr_data.get("limit", str(page_size)))
            offset += limit
            if offset >= total:
                break
            if self.sleep_seconds > 0:
                time.sleep(self.sleep_seconds)
        return merged_payload if merged_payload is not None else {"MRData": {}}

    def _request(self, endpoint: str, limit: int, offset: int) -> dict[str, Any]:
        url = f"{API_BASE}/{endpoint}.json?limit={limit}&offset={offset}"
        retriable = (urllib.error.HTTPError, urllib.error.URLError, TimeoutError)
        last_exc: Exception | None = None
        for attempt in range(1, self.max_retries + 1):
            self._throttle()
            try:
                with urllib.request.urlopen(url, timeout=30) as resp:
                    return json.loads(resp.read().decode("utf-8"))
            except retriable as exc:
                last_exc = exc
                if isinstance(exc, urllib.error.HTTPError) and self._should_give_up_http(exc):
                    break
                if attempt >= self.max_retries:
                    break
                delay = self._retry_delay_seconds(exc, attempt)
                print(
                    f"    retry {attempt}/{self.max_retries - 1} after {delay:.1f}s: {url}",
                    flush=True,
                )
                time.sleep(delay)
        raise RuntimeError(f"Failed to fetch {url}") from last_exc

    def _throttle(self) -> None:
        if self.sleep_seconds <= 0:
            return
        now = time.monotonic()
        elapsed = now - self._last_request_ts
        if elapsed < self.sleep_seconds:
            time.sleep(self.sleep_seconds - elapsed)
        self._last_request_ts = time.monotonic()

    def _retry_delay_seconds(self, exc: Exception, attempt: int) -> float:
        # 429 responses may include Retry-After; prefer that when present.
        if isinstance(exc, urllib.error.HTTPError) and exc.code == 429:
            retry_after = self._parse_retry_after_seconds(exc)
            if retry_after is not None:
                return max(retry_after, self.sleep_seconds)
        base = max(self.sleep_seconds, 0.5)
        return min(60.0, base * (2**attempt))

    @staticmethod
    def _parse_retry_after_seconds(exc: urllib.error.HTTPError) -> float | None:
        header = exc.headers.get("Retry-After")
        if not header:
            return None
        try:
            return max(0.0, float(header))
        except ValueError:
            try:
                retry_dt = parsedate_to_datetime(header)
                now_dt = dt.datetime.now(retry_dt.tzinfo or dt.timezone.utc)
                return max(0.0, (retry_dt - now_dt).total_seconds())
            except (TypeError, ValueError, OverflowError):
                return None

    @staticmethod
    def _should_give_up_http(exc: Exception) -> bool:
        if not isinstance(exc, urllib.error.HTTPError):
            return False
        return exc.code not in (408, 425, 429, 500, 502, 503, 504)

    @staticmethod
    def _locate_list_path(mr_data: dict[str, Any]) -> tuple[str, ...]:
        table_key = next((k for k in mr_data.keys() if k.endswith("Table")), None)
        if table_key is None:
            raise ValueError(f"Missing *Table key in response: {mr_data.keys()}")
        list_key = next((k for k, v in mr_data[table_key].items() if isinstance(v, list)), None)
        if list_key is None:
            raise ValueError(f"Missing list key in {table_key}: {mr_data[table_key].keys()}")
        return table_key, list_key

    @staticmethod
    def _dig(payload: dict[str, Any], path: tuple[str, ...]) -> list[Any]:
        cur: Any = payload
        for key in path:
            cur = cur[key]
        if not isinstance(cur, list):
            raise TypeError("Expected list while traversing payload")
        return cur


class F1Warehouse:
    def __init__(self, db_path: Path):
        self.conn = sqlite3.connect(db_path)
        self.conn.execute("PRAGMA foreign_keys = ON;")
        self.conn.execute("PRAGMA journal_mode = WAL;")
        self.conn.execute("PRAGMA synchronous = NORMAL;")

    def close(self) -> None:
        self.conn.close()

    def create_schema(self) -> None:
        self.conn.executescript(
            """
            CREATE TABLE IF NOT EXISTS seasons (
                season_year INTEGER PRIMARY KEY
            );
            CREATE TABLE IF NOT EXISTS circuits (
                circuit_id TEXT PRIMARY KEY,
                url TEXT,
                circuit_name TEXT,
                locality TEXT,
                country TEXT,
                lat REAL,
                lng REAL
            );
            CREATE TABLE IF NOT EXISTS races (
                race_id INTEGER PRIMARY KEY AUTOINCREMENT,
                season_year INTEGER NOT NULL REFERENCES seasons(season_year),
                round INTEGER NOT NULL,
                race_name TEXT NOT NULL,
                url TEXT,
                date TEXT,
                time TEXT,
                circuit_id TEXT REFERENCES circuits(circuit_id),
                fp1_date TEXT,
                fp1_time TEXT,
                fp2_date TEXT,
                fp2_time TEXT,
                fp3_date TEXT,
                fp3_time TEXT,
                qualifying_date TEXT,
                qualifying_time TEXT,
                sprint_date TEXT,
                sprint_time TEXT,
                UNIQUE(season_year, round)
            );
            CREATE TABLE IF NOT EXISTS drivers (
                driver_id TEXT PRIMARY KEY,
                permanent_number TEXT,
                code TEXT,
                given_name TEXT,
                family_name TEXT,
                date_of_birth TEXT,
                nationality TEXT,
                url TEXT
            );
            CREATE TABLE IF NOT EXISTS constructors (
                constructor_id TEXT PRIMARY KEY,
                name TEXT,
                nationality TEXT,
                url TEXT
            );
            CREATE TABLE IF NOT EXISTS status_codes (
                status_id INTEGER PRIMARY KEY,
                status TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS race_results (
                race_id INTEGER NOT NULL REFERENCES races(race_id),
                driver_id TEXT NOT NULL REFERENCES drivers(driver_id),
                constructor_id TEXT REFERENCES constructors(constructor_id),
                number TEXT,
                grid INTEGER,
                position INTEGER,
                position_text TEXT,
                position_order INTEGER,
                points REAL,
                laps INTEGER,
                status_id INTEGER REFERENCES status_codes(status_id),
                race_time_millis INTEGER,
                race_time_text TEXT,
                fastest_lap_rank INTEGER,
                fastest_lap_number INTEGER,
                fastest_lap_time TEXT,
                fastest_lap_avg_speed_kph REAL,
                PRIMARY KEY (race_id, driver_id)
            );
            CREATE TABLE IF NOT EXISTS qualifying_results (
                race_id INTEGER NOT NULL REFERENCES races(race_id),
                driver_id TEXT NOT NULL REFERENCES drivers(driver_id),
                constructor_id TEXT REFERENCES constructors(constructor_id),
                number TEXT,
                position INTEGER,
                q1 TEXT,
                q2 TEXT,
                q3 TEXT,
                PRIMARY KEY (race_id, driver_id)
            );
            CREATE TABLE IF NOT EXISTS sprint_results (
                race_id INTEGER NOT NULL REFERENCES races(race_id),
                driver_id TEXT NOT NULL REFERENCES drivers(driver_id),
                constructor_id TEXT REFERENCES constructors(constructor_id),
                number TEXT,
                grid INTEGER,
                position INTEGER,
                position_text TEXT,
                position_order INTEGER,
                points REAL,
                laps INTEGER,
                status_id INTEGER REFERENCES status_codes(status_id),
                race_time_millis INTEGER,
                race_time_text TEXT,
                fastest_lap_rank INTEGER,
                fastest_lap_number INTEGER,
                fastest_lap_time TEXT,
                fastest_lap_avg_speed_kph REAL,
                PRIMARY KEY (race_id, driver_id)
            );
            CREATE TABLE IF NOT EXISTS driver_standings (
                race_id INTEGER NOT NULL REFERENCES races(race_id),
                driver_id TEXT NOT NULL REFERENCES drivers(driver_id),
                points REAL,
                position INTEGER,
                position_text TEXT,
                wins INTEGER,
                PRIMARY KEY (race_id, driver_id)
            );
            CREATE TABLE IF NOT EXISTS constructor_standings (
                race_id INTEGER NOT NULL REFERENCES races(race_id),
                constructor_id TEXT NOT NULL REFERENCES constructors(constructor_id),
                points REAL,
                position INTEGER,
                position_text TEXT,
                wins INTEGER,
                PRIMARY KEY (race_id, constructor_id)
            );
            CREATE TABLE IF NOT EXISTS lap_times (
                race_id INTEGER NOT NULL REFERENCES races(race_id),
                lap INTEGER NOT NULL,
                driver_id TEXT NOT NULL REFERENCES drivers(driver_id),
                position INTEGER NOT NULL,
                time_text TEXT,
                time_millis INTEGER,
                PRIMARY KEY (race_id, lap, driver_id, position)
            );
            CREATE TABLE IF NOT EXISTS pit_stops (
                race_id INTEGER NOT NULL REFERENCES races(race_id),
                driver_id TEXT NOT NULL REFERENCES drivers(driver_id),
                stop INTEGER NOT NULL,
                lap INTEGER,
                time_of_day TEXT,
                duration_text TEXT,
                duration_millis INTEGER,
                PRIMARY KEY (race_id, driver_id, stop)
            );
            CREATE INDEX IF NOT EXISTS idx_races_season_round ON races(season_year, round);
            CREATE INDEX IF NOT EXISTS idx_results_driver ON race_results(driver_id);
            CREATE INDEX IF NOT EXISTS idx_results_constructor ON race_results(constructor_id);
            CREATE INDEX IF NOT EXISTS idx_laps_driver ON lap_times(driver_id);
            CREATE INDEX IF NOT EXISTS idx_pits_driver ON pit_stops(driver_id);

            CREATE TABLE IF NOT EXISTS driver_race_features (
                race_id INTEGER NOT NULL REFERENCES races(race_id),
                driver_id TEXT NOT NULL REFERENCES drivers(driver_id),
                constructor_id TEXT REFERENCES constructors(constructor_id),
                season_year INTEGER NOT NULL,
                round INTEGER NOT NULL,
                grid INTEGER,
                finish_position INTEGER,
                points REAL,
                laps_completed INTEGER,
                race_time_millis INTEGER,
                status_id INTEGER REFERENCES status_codes(status_id),
                constructor_standing_position INTEGER,
                constructor_standing_points REAL,
                driver_standing_position INTEGER,
                driver_standing_points REAL,
                pit_stop_count INTEGER,
                pit_total_duration_millis INTEGER,
                avg_lap_time_millis REAL,
                best_lap_time_millis INTEGER,
                rolling3_avg_finish_position REAL,
                rolling3_avg_points REAL,
                rolling5_avg_finish_position REAL,
                rolling5_avg_points REAL,
                age_days INTEGER,
                target_finish_position INTEGER,
                target_points REAL,
                target_is_podium INTEGER,
                target_is_win INTEGER,
                PRIMARY KEY (race_id, driver_id)
            );
            CREATE INDEX IF NOT EXISTS idx_driver_features_driver ON driver_race_features(driver_id);
            CREATE INDEX IF NOT EXISTS idx_driver_features_season_round ON driver_race_features(season_year, round);

            CREATE TABLE IF NOT EXISTS winner_features_best_driver (
                race_id INTEGER NOT NULL REFERENCES races(race_id),
                driver_id TEXT NOT NULL REFERENCES drivers(driver_id),
                constructor_id TEXT REFERENCES constructors(constructor_id),
                season_year INTEGER NOT NULL,
                round INTEGER NOT NULL,
                grid INTEGER,
                qual_position INTEGER,
                prev_driver_standing_position INTEGER,
                prev_driver_standing_points REAL,
                prev_constructor_standing_position INTEGER,
                prev_constructor_standing_points REAL,
                driver_avg_finish_prev3 REAL,
                driver_avg_points_prev3 REAL,
                driver_win_rate_prev8 REAL,
                driver_podium_rate_prev8 REAL,
                driver_dnf_rate_prev8 REAL,
                car_avg_points_prev5 REAL,
                car_win_rate_prev8 REAL,
                car_dnf_rate_prev8 REAL,
                target_is_win INTEGER NOT NULL,
                target_constructor_win INTEGER NOT NULL,
                PRIMARY KEY (race_id, driver_id)
            );
            CREATE TABLE IF NOT EXISTS winner_features_best_car (
                race_id INTEGER NOT NULL REFERENCES races(race_id),
                driver_id TEXT NOT NULL REFERENCES drivers(driver_id),
                constructor_id TEXT REFERENCES constructors(constructor_id),
                season_year INTEGER NOT NULL,
                round INTEGER NOT NULL,
                grid INTEGER,
                qual_position INTEGER,
                constructor_entries_in_race INTEGER,
                constructor_mean_grid REAL,
                constructor_best_grid INTEGER,
                prev_constructor_standing_position INTEGER,
                prev_constructor_standing_points REAL,
                car_avg_points_prev5 REAL,
                car_avg_finish_prev5 REAL,
                car_win_rate_prev8 REAL,
                car_podium_rate_prev8 REAL,
                car_dnf_rate_prev8 REAL,
                driver_avg_points_prev5 REAL,
                driver_win_rate_prev8 REAL,
                target_is_win INTEGER NOT NULL,
                target_constructor_win INTEGER NOT NULL,
                PRIMARY KEY (race_id, driver_id)
            );
            CREATE INDEX IF NOT EXISTS idx_winner_driver_features ON winner_features_best_driver(driver_id);
            CREATE INDEX IF NOT EXISTS idx_winner_car_features ON winner_features_best_car(constructor_id);

            CREATE VIEW IF NOT EXISTS v_driver_race_features AS
            WITH pit_agg AS (
                SELECT race_id, driver_id, COUNT(*) AS pit_stop_count, SUM(duration_millis) AS pit_total_duration_millis
                FROM pit_stops
                GROUP BY race_id, driver_id
            ),
            lap_agg AS (
                SELECT race_id, driver_id, AVG(time_millis) AS avg_lap_time_millis, MIN(time_millis) AS best_lap_time_millis
                FROM lap_times
                GROUP BY race_id, driver_id
            ),
            base AS (
                SELECT
                    rr.race_id, rr.driver_id, rr.constructor_id, r.season_year, r.round, rr.grid,
                    rr.position AS finish_position, rr.points, rr.laps AS laps_completed, rr.race_time_millis, rr.status_id,
                    cs.position AS constructor_standing_position, cs.points AS constructor_standing_points,
                    ds.position AS driver_standing_position, ds.points AS driver_standing_points,
                    COALESCE(pa.pit_stop_count, 0) AS pit_stop_count, pa.pit_total_duration_millis,
                    la.avg_lap_time_millis, la.best_lap_time_millis,
                    CAST(julianday(r.date) - julianday(d.date_of_birth) AS INTEGER) AS age_days
                FROM race_results rr
                JOIN races r ON r.race_id = rr.race_id
                LEFT JOIN drivers d ON d.driver_id = rr.driver_id
                LEFT JOIN driver_standings ds ON ds.race_id = rr.race_id AND ds.driver_id = rr.driver_id
                LEFT JOIN constructor_standings cs ON cs.race_id = rr.race_id AND cs.constructor_id = rr.constructor_id
                LEFT JOIN pit_agg pa ON pa.race_id = rr.race_id AND pa.driver_id = rr.driver_id
                LEFT JOIN lap_agg la ON la.race_id = rr.race_id AND la.driver_id = rr.driver_id
            )
            SELECT
                b.*,
                AVG(b.finish_position) OVER (PARTITION BY b.driver_id ORDER BY b.season_year, b.round ROWS BETWEEN 3 PRECEDING AND 1 PRECEDING) AS rolling3_avg_finish_position,
                AVG(b.points) OVER (PARTITION BY b.driver_id ORDER BY b.season_year, b.round ROWS BETWEEN 3 PRECEDING AND 1 PRECEDING) AS rolling3_avg_points,
                AVG(b.finish_position) OVER (PARTITION BY b.driver_id ORDER BY b.season_year, b.round ROWS BETWEEN 5 PRECEDING AND 1 PRECEDING) AS rolling5_avg_finish_position,
                AVG(b.points) OVER (PARTITION BY b.driver_id ORDER BY b.season_year, b.round ROWS BETWEEN 5 PRECEDING AND 1 PRECEDING) AS rolling5_avg_points,
                b.finish_position AS target_finish_position,
                b.points AS target_points,
                CASE WHEN b.finish_position <= 3 THEN 1 ELSE 0 END AS target_is_podium,
                CASE WHEN b.finish_position = 1 THEN 1 ELSE 0 END AS target_is_win
            FROM base b;

            CREATE VIEW IF NOT EXISTS v_winner_features_base AS
            WITH race_driver_base AS (
                SELECT
                    rr.race_id, rr.driver_id, rr.constructor_id, r.season_year, r.round,
                    rr.grid, qr.position AS qual_position, rr.position AS finish_position, rr.points,
                    CASE WHEN rr.position = 1 THEN 1 ELSE 0 END AS target_is_win
                FROM race_results rr
                JOIN races r ON r.race_id = rr.race_id
                LEFT JOIN qualifying_results qr ON qr.race_id = rr.race_id AND qr.driver_id = rr.driver_id
            ),
            race_constructor_win AS (
                SELECT rr.race_id, rr.constructor_id, MAX(CASE WHEN rr.position = 1 THEN 1 ELSE 0 END) AS target_constructor_win
                FROM race_results rr
                GROUP BY rr.race_id, rr.constructor_id
            ),
            driver_prev AS (
                SELECT
                    ds.driver_id, r.season_year, r.round,
                    LAG(ds.position) OVER (PARTITION BY ds.driver_id, r.season_year ORDER BY r.round) AS prev_driver_standing_position,
                    LAG(ds.points) OVER (PARTITION BY ds.driver_id, r.season_year ORDER BY r.round) AS prev_driver_standing_points
                FROM driver_standings ds
                JOIN races r ON r.race_id = ds.race_id
            ),
            constructor_prev AS (
                SELECT
                    cs.constructor_id, r.season_year, r.round,
                    LAG(cs.position) OVER (PARTITION BY cs.constructor_id, r.season_year ORDER BY r.round) AS prev_constructor_standing_position,
                    LAG(cs.points) OVER (PARTITION BY cs.constructor_id, r.season_year ORDER BY r.round) AS prev_constructor_standing_points
                FROM constructor_standings cs
                JOIN races r ON r.race_id = cs.race_id
            ),
            constructor_race_perf AS (
                SELECT
                    rr.race_id, rr.constructor_id, r.season_year, r.round,
                    SUM(rr.points) AS constructor_points_sum,
                    MIN(rr.position) AS constructor_best_finish,
                    MAX(CASE WHEN rr.position = 1 THEN 1 ELSE 0 END) AS constructor_has_win,
                    MAX(CASE WHEN rr.position <= 3 THEN 1 ELSE 0 END) AS constructor_has_podium,
                    AVG(CASE WHEN rr.position IS NULL THEN 1.0 ELSE 0.0 END) AS constructor_dnf_rate
                FROM race_results rr
                JOIN races r ON r.race_id = rr.race_id
                GROUP BY rr.race_id, rr.constructor_id
            ),
            constructor_form AS (
                SELECT
                    c.*,
                    AVG(c.constructor_points_sum * 1.0) OVER (PARTITION BY c.constructor_id ORDER BY c.season_year, c.round ROWS BETWEEN 5 PRECEDING AND 1 PRECEDING) AS car_avg_points_prev5,
                    AVG(c.constructor_best_finish * 1.0) OVER (PARTITION BY c.constructor_id ORDER BY c.season_year, c.round ROWS BETWEEN 5 PRECEDING AND 1 PRECEDING) AS car_avg_finish_prev5,
                    AVG(c.constructor_has_win * 1.0) OVER (PARTITION BY c.constructor_id ORDER BY c.season_year, c.round ROWS BETWEEN 8 PRECEDING AND 1 PRECEDING) AS car_win_rate_prev8,
                    AVG(c.constructor_has_podium * 1.0) OVER (PARTITION BY c.constructor_id ORDER BY c.season_year, c.round ROWS BETWEEN 8 PRECEDING AND 1 PRECEDING) AS car_podium_rate_prev8,
                    AVG(c.constructor_dnf_rate) OVER (PARTITION BY c.constructor_id ORDER BY c.season_year, c.round ROWS BETWEEN 8 PRECEDING AND 1 PRECEDING) AS car_dnf_rate_prev8
                FROM constructor_race_perf c
            ),
            driver_form AS (
                SELECT
                    b.*,
                    AVG(CASE WHEN b.finish_position IS NOT NULL THEN b.finish_position * 1.0 END) OVER (PARTITION BY b.driver_id ORDER BY b.season_year, b.round ROWS BETWEEN 3 PRECEDING AND 1 PRECEDING) AS driver_avg_finish_prev3,
                    AVG(b.points * 1.0) OVER (PARTITION BY b.driver_id ORDER BY b.season_year, b.round ROWS BETWEEN 3 PRECEDING AND 1 PRECEDING) AS driver_avg_points_prev3,
                    AVG(b.points * 1.0) OVER (PARTITION BY b.driver_id ORDER BY b.season_year, b.round ROWS BETWEEN 5 PRECEDING AND 1 PRECEDING) AS driver_avg_points_prev5,
                    AVG(CASE WHEN b.finish_position = 1 THEN 1.0 ELSE 0.0 END) OVER (PARTITION BY b.driver_id ORDER BY b.season_year, b.round ROWS BETWEEN 8 PRECEDING AND 1 PRECEDING) AS driver_win_rate_prev8,
                    AVG(CASE WHEN b.finish_position <= 3 THEN 1.0 ELSE 0.0 END) OVER (PARTITION BY b.driver_id ORDER BY b.season_year, b.round ROWS BETWEEN 8 PRECEDING AND 1 PRECEDING) AS driver_podium_rate_prev8,
                    AVG(CASE WHEN b.finish_position IS NULL THEN 1.0 ELSE 0.0 END) OVER (PARTITION BY b.driver_id ORDER BY b.season_year, b.round ROWS BETWEEN 8 PRECEDING AND 1 PRECEDING) AS driver_dnf_rate_prev8,
                    COUNT(*) OVER (PARTITION BY b.race_id, b.constructor_id) AS constructor_entries_in_race,
                    AVG(b.grid * 1.0) OVER (PARTITION BY b.race_id, b.constructor_id) AS constructor_mean_grid,
                    MIN(b.grid) OVER (PARTITION BY b.race_id, b.constructor_id) AS constructor_best_grid
                FROM race_driver_base b
            )
            SELECT
                df.race_id, df.driver_id, df.constructor_id, df.season_year, df.round, df.grid, df.qual_position,
                dp.prev_driver_standing_position, dp.prev_driver_standing_points,
                cp.prev_constructor_standing_position, cp.prev_constructor_standing_points,
                df.driver_avg_finish_prev3, df.driver_avg_points_prev3, df.driver_avg_points_prev5,
                df.driver_win_rate_prev8, df.driver_podium_rate_prev8, df.driver_dnf_rate_prev8,
                cf.car_avg_points_prev5, cf.car_avg_finish_prev5, cf.car_win_rate_prev8, cf.car_podium_rate_prev8, cf.car_dnf_rate_prev8,
                df.constructor_entries_in_race, df.constructor_mean_grid, df.constructor_best_grid,
                df.target_is_win, COALESCE(rcw.target_constructor_win, 0) AS target_constructor_win
            FROM driver_form df
            LEFT JOIN driver_prev dp ON dp.driver_id = df.driver_id AND dp.season_year = df.season_year AND dp.round = df.round
            LEFT JOIN constructor_prev cp ON cp.constructor_id = df.constructor_id AND cp.season_year = df.season_year AND cp.round = df.round
            LEFT JOIN constructor_form cf ON cf.race_id = df.race_id AND cf.constructor_id = df.constructor_id
            LEFT JOIN race_constructor_win rcw ON rcw.race_id = df.race_id AND rcw.constructor_id = df.constructor_id;

            CREATE VIEW IF NOT EXISTS v_winner_features_best_driver AS
            SELECT
                race_id, driver_id, constructor_id, season_year, round, grid, qual_position,
                prev_driver_standing_position, prev_driver_standing_points,
                prev_constructor_standing_position, prev_constructor_standing_points,
                driver_avg_finish_prev3, driver_avg_points_prev3, driver_win_rate_prev8, driver_podium_rate_prev8, driver_dnf_rate_prev8,
                car_avg_points_prev5, car_win_rate_prev8, car_dnf_rate_prev8,
                target_is_win, target_constructor_win
            FROM v_winner_features_base;

            CREATE VIEW IF NOT EXISTS v_winner_features_best_car AS
            SELECT
                race_id, driver_id, constructor_id, season_year, round, grid, qual_position,
                constructor_entries_in_race, constructor_mean_grid, constructor_best_grid,
                prev_constructor_standing_position, prev_constructor_standing_points,
                car_avg_points_prev5, car_avg_finish_prev5, car_win_rate_prev8, car_podium_rate_prev8, car_dnf_rate_prev8,
                driver_avg_points_prev5, driver_win_rate_prev8,
                target_is_win, target_constructor_win
            FROM v_winner_features_base;
            """
        )
        self.conn.commit()

    def commit(self) -> None:
        self.conn.commit()

    def upsert_season(self, year: int) -> None:
        self.conn.execute("INSERT OR IGNORE INTO seasons(season_year) VALUES (?)", (year,))

    def upsert_circuit(self, circuit: dict[str, Any]) -> None:
        location = circuit.get("Location", {})
        self.conn.execute(
            """
            INSERT INTO circuits(circuit_id, url, circuit_name, locality, country, lat, lng)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(circuit_id) DO UPDATE SET
                url=excluded.url, circuit_name=excluded.circuit_name, locality=excluded.locality,
                country=excluded.country, lat=excluded.lat, lng=excluded.lng
            """,
            (
                circuit.get("circuitId"),
                circuit.get("url"),
                circuit.get("circuitName"),
                location.get("locality"),
                location.get("country"),
                _as_float(location.get("lat")),
                _as_float(location.get("long")),
            ),
        )

    def upsert_race(self, race: dict[str, Any], season_year: int) -> int:
        fp1 = race.get("FirstPractice", {})
        fp2 = race.get("SecondPractice", {})
        fp3 = race.get("ThirdPractice", {})
        qual = race.get("Qualifying", {})
        sprint = race.get("Sprint", {})
        self.conn.execute(
            """
            INSERT INTO races(
                season_year, round, race_name, url, date, time, circuit_id,
                fp1_date, fp1_time, fp2_date, fp2_time, fp3_date, fp3_time,
                qualifying_date, qualifying_time, sprint_date, sprint_time
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(season_year, round) DO UPDATE SET
                race_name=excluded.race_name, url=excluded.url, date=excluded.date, time=excluded.time,
                circuit_id=excluded.circuit_id, fp1_date=excluded.fp1_date, fp1_time=excluded.fp1_time,
                fp2_date=excluded.fp2_date, fp2_time=excluded.fp2_time, fp3_date=excluded.fp3_date,
                fp3_time=excluded.fp3_time, qualifying_date=excluded.qualifying_date,
                qualifying_time=excluded.qualifying_time, sprint_date=excluded.sprint_date, sprint_time=excluded.sprint_time
            """,
            (
                season_year,
                _as_int(race.get("round")),
                race.get("raceName"),
                race.get("url"),
                race.get("date"),
                race.get("time"),
                race.get("Circuit", {}).get("circuitId"),
                fp1.get("date"),
                fp1.get("time"),
                fp2.get("date"),
                fp2.get("time"),
                fp3.get("date"),
                fp3.get("time"),
                qual.get("date"),
                qual.get("time"),
                sprint.get("date"),
                sprint.get("time"),
            ),
        )
        row = self.conn.execute(
            "SELECT race_id FROM races WHERE season_year=? AND round=?",
            (season_year, _as_int(race.get("round"))),
        ).fetchone()
        if row is None:
            raise RuntimeError("Could not resolve race_id after upsert")
        return int(row[0])

    def upsert_driver(self, driver: dict[str, Any]) -> None:
        self.conn.execute(
            """
            INSERT INTO drivers(driver_id, permanent_number, code, given_name, family_name, date_of_birth, nationality, url)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(driver_id) DO UPDATE SET
                permanent_number=excluded.permanent_number, code=excluded.code, given_name=excluded.given_name,
                family_name=excluded.family_name, date_of_birth=excluded.date_of_birth, nationality=excluded.nationality, url=excluded.url
            """,
            (
                driver.get("driverId"),
                driver.get("permanentNumber"),
                driver.get("code"),
                driver.get("givenName"),
                driver.get("familyName"),
                driver.get("dateOfBirth"),
                driver.get("nationality"),
                driver.get("url"),
            ),
        )

    def upsert_constructor(self, constructor: dict[str, Any]) -> None:
        self.conn.execute(
            """
            INSERT INTO constructors(constructor_id, name, nationality, url)
            VALUES (?, ?, ?, ?)
            ON CONFLICT(constructor_id) DO UPDATE SET
                name=excluded.name, nationality=excluded.nationality, url=excluded.url
            """,
            (
                constructor.get("constructorId"),
                constructor.get("name"),
                constructor.get("nationality"),
                constructor.get("url"),
            ),
        )

    def upsert_status(self, status: str) -> int:
        row = self.conn.execute("SELECT status_id FROM status_codes WHERE status=?", (status,)).fetchone()
        if row is not None:
            return int(row[0])
        next_id = self.conn.execute("SELECT COALESCE(MAX(status_id),0)+1 FROM status_codes").fetchone()[0]
        self.conn.execute("INSERT INTO status_codes(status_id, status) VALUES (?, ?)", (int(next_id), status))
        return int(next_id)

    def upsert_race_result(self, race_id: int, result: dict[str, Any]) -> None:
        self.upsert_driver(result.get("Driver", {}))
        self.upsert_constructor(result.get("Constructor", {}))
        status_id = self.upsert_status(result.get("status", "Unknown"))
        fastest = result.get("FastestLap", {})
        self.conn.execute(
            """
            INSERT INTO race_results(
                race_id, driver_id, constructor_id, number, grid, position, position_text, position_order, points,
                laps, status_id, race_time_millis, race_time_text, fastest_lap_rank, fastest_lap_number,
                fastest_lap_time, fastest_lap_avg_speed_kph
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(race_id, driver_id) DO UPDATE SET
                constructor_id=excluded.constructor_id, number=excluded.number, grid=excluded.grid, position=excluded.position,
                position_text=excluded.position_text, position_order=excluded.position_order, points=excluded.points, laps=excluded.laps,
                status_id=excluded.status_id, race_time_millis=excluded.race_time_millis, race_time_text=excluded.race_time_text,
                fastest_lap_rank=excluded.fastest_lap_rank, fastest_lap_number=excluded.fastest_lap_number,
                fastest_lap_time=excluded.fastest_lap_time, fastest_lap_avg_speed_kph=excluded.fastest_lap_avg_speed_kph
            """,
            (
                race_id,
                result.get("Driver", {}).get("driverId"),
                result.get("Constructor", {}).get("constructorId"),
                result.get("number"),
                _as_int(result.get("grid")),
                _as_int(result.get("position")),
                result.get("positionText"),
                _as_int(result.get("positionOrder")),
                _as_float(result.get("points")),
                _as_int(result.get("laps")),
                status_id,
                _as_int(result.get("Time", {}).get("millis")),
                result.get("Time", {}).get("time"),
                _as_int(fastest.get("rank")),
                _as_int(fastest.get("lap")),
                fastest.get("Time", {}).get("time"),
                _as_float(fastest.get("AverageSpeed", {}).get("speed")),
            ),
        )

    def upsert_qualifying(self, race_id: int, result: dict[str, Any]) -> None:
        self.upsert_driver(result.get("Driver", {}))
        self.upsert_constructor(result.get("Constructor", {}))
        self.conn.execute(
            """
            INSERT INTO qualifying_results(race_id, driver_id, constructor_id, number, position, q1, q2, q3)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(race_id, driver_id) DO UPDATE SET
                constructor_id=excluded.constructor_id, number=excluded.number, position=excluded.position,
                q1=excluded.q1, q2=excluded.q2, q3=excluded.q3
            """,
            (
                race_id,
                result.get("Driver", {}).get("driverId"),
                result.get("Constructor", {}).get("constructorId"),
                result.get("number"),
                _as_int(result.get("position")),
                result.get("Q1"),
                result.get("Q2"),
                result.get("Q3"),
            ),
        )

    def upsert_sprint(self, race_id: int, result: dict[str, Any]) -> None:
        self.upsert_driver(result.get("Driver", {}))
        self.upsert_constructor(result.get("Constructor", {}))
        status_id = self.upsert_status(result.get("status", "Unknown"))
        fastest = result.get("FastestLap", {})
        self.conn.execute(
            """
            INSERT INTO sprint_results(
                race_id, driver_id, constructor_id, number, grid, position, position_text, position_order, points,
                laps, status_id, race_time_millis, race_time_text, fastest_lap_rank, fastest_lap_number,
                fastest_lap_time, fastest_lap_avg_speed_kph
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(race_id, driver_id) DO UPDATE SET
                constructor_id=excluded.constructor_id, number=excluded.number, grid=excluded.grid, position=excluded.position,
                position_text=excluded.position_text, position_order=excluded.position_order, points=excluded.points, laps=excluded.laps,
                status_id=excluded.status_id, race_time_millis=excluded.race_time_millis, race_time_text=excluded.race_time_text,
                fastest_lap_rank=excluded.fastest_lap_rank, fastest_lap_number=excluded.fastest_lap_number,
                fastest_lap_time=excluded.fastest_lap_time, fastest_lap_avg_speed_kph=excluded.fastest_lap_avg_speed_kph
            """,
            (
                race_id,
                result.get("Driver", {}).get("driverId"),
                result.get("Constructor", {}).get("constructorId"),
                result.get("number"),
                _as_int(result.get("grid")),
                _as_int(result.get("position")),
                result.get("positionText"),
                _as_int(result.get("positionOrder")),
                _as_float(result.get("points")),
                _as_int(result.get("laps")),
                status_id,
                _as_int(result.get("Time", {}).get("millis")),
                result.get("Time", {}).get("time"),
                _as_int(fastest.get("rank")),
                _as_int(fastest.get("lap")),
                fastest.get("Time", {}).get("time"),
                _as_float(fastest.get("AverageSpeed", {}).get("speed")),
            ),
        )

    def upsert_driver_standing(self, race_id: int, standing: dict[str, Any]) -> None:
        self.upsert_driver(standing.get("Driver", {}))
        self.conn.execute(
            """
            INSERT INTO driver_standings(race_id, driver_id, points, position, position_text, wins)
            VALUES (?, ?, ?, ?, ?, ?)
            ON CONFLICT(race_id, driver_id) DO UPDATE SET
                points=excluded.points, position=excluded.position, position_text=excluded.position_text, wins=excluded.wins
            """,
            (
                race_id,
                standing.get("Driver", {}).get("driverId"),
                _as_float(standing.get("points")),
                _as_int(standing.get("position")),
                standing.get("positionText"),
                _as_int(standing.get("wins")),
            ),
        )

    def upsert_constructor_standing(self, race_id: int, standing: dict[str, Any]) -> None:
        constructor = standing.get("Constructor", {})
        self.upsert_constructor(constructor)
        self.conn.execute(
            """
            INSERT INTO constructor_standings(race_id, constructor_id, points, position, position_text, wins)
            VALUES (?, ?, ?, ?, ?, ?)
            ON CONFLICT(race_id, constructor_id) DO UPDATE SET
                points=excluded.points, position=excluded.position, position_text=excluded.position_text, wins=excluded.wins
            """,
            (
                race_id,
                constructor.get("constructorId"),
                _as_float(standing.get("points")),
                _as_int(standing.get("position")),
                standing.get("positionText"),
                _as_int(standing.get("wins")),
            ),
        )

    def upsert_lap_times(self, race_id: int, lap_payload: dict[str, Any]) -> None:
        lap_number = _as_int(lap_payload.get("number"))
        for timing in lap_payload.get("Timings", []):
            driver_id = timing.get("driverId")
            if not driver_id:
                continue
            self.conn.execute(
                """
                INSERT INTO lap_times(race_id, lap, driver_id, position, time_text, time_millis)
                VALUES (?, ?, ?, ?, ?, ?)
                ON CONFLICT(race_id, lap, driver_id, position) DO UPDATE SET
                    time_text=excluded.time_text, time_millis=excluded.time_millis
                """,
                (
                    race_id,
                    lap_number,
                    driver_id,
                    _as_int(timing.get("position")),
                    timing.get("time"),
                    _mmss_to_millis(timing.get("time")),
                ),
            )

    def upsert_pit_stop(self, race_id: int, stop: dict[str, Any]) -> None:
        driver_id = stop.get("driverId")
        if not driver_id:
            return
        self.conn.execute(
            """
            INSERT INTO pit_stops(race_id, driver_id, stop, lap, time_of_day, duration_text, duration_millis)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(race_id, driver_id, stop) DO UPDATE SET
                lap=excluded.lap, time_of_day=excluded.time_of_day,
                duration_text=excluded.duration_text, duration_millis=excluded.duration_millis
            """,
            (
                race_id,
                driver_id,
                _as_int(stop.get("stop")),
                _as_int(stop.get("lap")),
                stop.get("time"),
                stop.get("duration"),
                _seconds_to_millis(stop.get("duration")),
            ),
        )

    def rebuild_feature_tables(self) -> None:
        self.conn.execute("DELETE FROM driver_race_features")
        self.conn.execute(
            """
            INSERT INTO driver_race_features(
                race_id, driver_id, constructor_id, season_year, round, grid, finish_position, points, laps_completed,
                race_time_millis, status_id, constructor_standing_position, constructor_standing_points,
                driver_standing_position, driver_standing_points, pit_stop_count, pit_total_duration_millis,
                avg_lap_time_millis, best_lap_time_millis, rolling3_avg_finish_position, rolling3_avg_points,
                rolling5_avg_finish_position, rolling5_avg_points, age_days, target_finish_position, target_points,
                target_is_podium, target_is_win
            )
            SELECT
                race_id, driver_id, constructor_id, season_year, round, grid, finish_position, points, laps_completed,
                race_time_millis, status_id, constructor_standing_position, constructor_standing_points,
                driver_standing_position, driver_standing_points, pit_stop_count, pit_total_duration_millis,
                avg_lap_time_millis, best_lap_time_millis, rolling3_avg_finish_position, rolling3_avg_points,
                rolling5_avg_finish_position, rolling5_avg_points, age_days, target_finish_position, target_points,
                target_is_podium, target_is_win
            FROM v_driver_race_features
            """
        )
        self.conn.execute("DELETE FROM winner_features_best_driver")
        self.conn.execute(
            """
            INSERT INTO winner_features_best_driver(
                race_id, driver_id, constructor_id, season_year, round, grid, qual_position,
                prev_driver_standing_position, prev_driver_standing_points, prev_constructor_standing_position,
                prev_constructor_standing_points, driver_avg_finish_prev3, driver_avg_points_prev3, driver_win_rate_prev8,
                driver_podium_rate_prev8, driver_dnf_rate_prev8, car_avg_points_prev5, car_win_rate_prev8,
                car_dnf_rate_prev8, target_is_win, target_constructor_win
            )
            SELECT
                race_id, driver_id, constructor_id, season_year, round, grid, qual_position,
                prev_driver_standing_position, prev_driver_standing_points, prev_constructor_standing_position,
                prev_constructor_standing_points, driver_avg_finish_prev3, driver_avg_points_prev3, driver_win_rate_prev8,
                driver_podium_rate_prev8, driver_dnf_rate_prev8, car_avg_points_prev5, car_win_rate_prev8,
                car_dnf_rate_prev8, target_is_win, target_constructor_win
            FROM v_winner_features_best_driver
            """
        )
        self.conn.execute("DELETE FROM winner_features_best_car")
        self.conn.execute(
            """
            INSERT INTO winner_features_best_car(
                race_id, driver_id, constructor_id, season_year, round, grid, qual_position,
                constructor_entries_in_race, constructor_mean_grid, constructor_best_grid,
                prev_constructor_standing_position, prev_constructor_standing_points, car_avg_points_prev5,
                car_avg_finish_prev5, car_win_rate_prev8, car_podium_rate_prev8, car_dnf_rate_prev8,
                driver_avg_points_prev5, driver_win_rate_prev8, target_is_win, target_constructor_win
            )
            SELECT
                race_id, driver_id, constructor_id, season_year, round, grid, qual_position,
                constructor_entries_in_race, constructor_mean_grid, constructor_best_grid,
                prev_constructor_standing_position, prev_constructor_standing_points, car_avg_points_prev5,
                car_avg_finish_prev5, car_win_rate_prev8, car_podium_rate_prev8, car_dnf_rate_prev8,
                driver_avg_points_prev5, driver_win_rate_prev8, target_is_win, target_constructor_win
            FROM v_winner_features_best_car
            """
        )
        self.conn.commit()


def _sync_results(client: JolpicaClient, warehouse: F1Warehouse, year: int, round_no: str, race_id: int) -> None:
    payload = client.fetch_all(f"{year}/{round_no}/results")
    for race in payload["MRData"]["RaceTable"].get("Races", []):
        for result in race.get("Results", []):
            warehouse.upsert_race_result(race_id, result)


def _sync_qualifying(client: JolpicaClient, warehouse: F1Warehouse, year: int, round_no: str, race_id: int) -> None:
    payload = client.fetch_all(f"{year}/{round_no}/qualifying")
    for race in payload["MRData"]["RaceTable"].get("Races", []):
        for result in race.get("QualifyingResults", []):
            warehouse.upsert_qualifying(race_id, result)


def _sync_sprint(client: JolpicaClient, warehouse: F1Warehouse, year: int, round_no: str, race_id: int) -> None:
    payload = client.fetch_all(f"{year}/{round_no}/sprint")
    for race in payload["MRData"]["RaceTable"].get("Races", []):
        for result in race.get("SprintResults", []):
            warehouse.upsert_sprint(race_id, result)


def _sync_driver_standings(client: JolpicaClient, warehouse: F1Warehouse, year: int, round_no: str, race_id: int) -> None:
    payload = client.fetch_all(f"{year}/{round_no}/driverStandings")
    for standings_list in payload["MRData"]["StandingsTable"].get("StandingsLists", []):
        for standing in standings_list.get("DriverStandings", []):
            warehouse.upsert_driver_standing(race_id, standing)


def _sync_constructor_standings(client: JolpicaClient, warehouse: F1Warehouse, year: int, round_no: str, race_id: int) -> None:
    payload = client.fetch_all(f"{year}/{round_no}/constructorStandings")
    for standings_list in payload["MRData"]["StandingsTable"].get("StandingsLists", []):
        for standing in standings_list.get("ConstructorStandings", []):
            warehouse.upsert_constructor_standing(race_id, standing)


def _sync_lap_times(client: JolpicaClient, warehouse: F1Warehouse, year: int, round_no: str, race_id: int) -> None:
    payload = client.fetch_all(f"{year}/{round_no}/laps", page_size=500)
    for race in payload["MRData"]["RaceTable"].get("Races", []):
        for lap in race.get("Laps", []):
            warehouse.upsert_lap_times(race_id, lap)


def _sync_pit_stops(client: JolpicaClient, warehouse: F1Warehouse, year: int, round_no: str, race_id: int) -> None:
    payload = client.fetch_all(f"{year}/{round_no}/pitstops")
    for race in payload["MRData"]["RaceTable"].get("Races", []):
        for stop in race.get("PitStops", []):
            warehouse.upsert_pit_stop(race_id, stop)


def sync_history(config: SyncConfig) -> None:
    client = JolpicaClient(config.sleep_seconds, config.max_retries)
    warehouse = F1Warehouse(config.db_path)
    warehouse.create_schema()
    try:
        for year in range(config.from_year, config.to_year + 1):
            print(f"[{year}] syncing season", flush=True)
            warehouse.upsert_season(year)
            races = client.fetch_all(f"{year}")["MRData"]["RaceTable"].get("Races", [])
            if not races:
                print(f"[{year}] no races found", flush=True)
                continue
            for race in races:
                round_no = _as_int(race.get("round"))
                if config.from_round is not None and (round_no is None or round_no < config.from_round):
                    continue
                if config.to_round is not None and (round_no is None or round_no > config.to_round):
                    continue
                round_str = race.get("round")
                warehouse.upsert_circuit(race.get("Circuit", {}))
                race_id = warehouse.upsert_race(race, year)
                print(f"  - round {round_no}: {race.get('raceName')}", flush=True)
                _sync_results(client, warehouse, year, round_str, race_id)
                _sync_qualifying(client, warehouse, year, round_str, race_id)
                _sync_sprint(client, warehouse, year, round_str, race_id)
                _sync_driver_standings(client, warehouse, year, round_str, race_id)
                _sync_constructor_standings(client, warehouse, year, round_str, race_id)
                if config.include_lap_times:
                    _sync_lap_times(client, warehouse, year, round_str, race_id)
                if config.include_pit_stops:
                    _sync_pit_stops(client, warehouse, year, round_str, race_id)
                warehouse.commit()
            print(f"[{year}] done", flush=True)
        if config.rebuild_features:
            print("rebuilding feature tables", flush=True)
            warehouse.rebuild_feature_tables()
            print("feature tables ready", flush=True)
    finally:
        warehouse.close()


def _as_int(value: Any) -> int | None:
    if value in (None, "", "\\N"):
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _as_float(value: Any) -> float | None:
    if value in (None, "", "\\N"):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _mmss_to_millis(text: str | None) -> int | None:
    if not text:
        return None
    parts = text.split(":")
    try:
        if len(parts) == 2:
            return int((int(parts[0]) * 60 + float(parts[1])) * 1000)
        return int(float(parts[0]) * 1000)
    except (TypeError, ValueError):
        return None


def _seconds_to_millis(text: str | None) -> int | None:
    if not text:
        return None
    try:
        return int(float(text) * 1000)
    except (TypeError, ValueError):
        return None


def parse_args() -> SyncConfig:
    parser = argparse.ArgumentParser(description="Build a local SQLite warehouse for historical F1 data.")
    parser.add_argument("--db", default="f1_history.db", type=Path, help="SQLite database output path")
    parser.add_argument("--from-year", type=int, default=1950, help="First season to ingest")
    parser.add_argument("--to-year", type=int, default=dt.date.today().year, help="Last season to ingest")
    parser.add_argument("--from-round", type=int, default=None, help="First round (inclusive) within each season")
    parser.add_argument("--to-round", type=int, default=None, help="Last round (inclusive) within each season")
    parser.add_argument("--sleep-seconds", type=float, default=0.2, help="Delay between paginated API requests")
    parser.add_argument("--max-retries", type=int, default=8, help="Max request retries for transient network errors")
    parser.add_argument("--skip-lap-times", action="store_true", help="Skip per-lap timing data ingestion")
    parser.add_argument("--skip-pit-stops", action="store_true", help="Skip pit stop data ingestion")
    parser.add_argument("--skip-feature-rebuild", action="store_true", help="Skip rebuilding materialized feature tables")
    args = parser.parse_args()
    if args.from_year < 1950:
        raise ValueError("from-year must be >= 1950")
    if args.to_year < args.from_year:
        raise ValueError("to-year must be >= from-year")
    if args.from_round is not None and args.from_round < 1:
        raise ValueError("from-round must be >= 1")
    if args.to_round is not None and args.to_round < 1:
        raise ValueError("to-round must be >= 1")
    if args.from_round is not None and args.to_round is not None and args.to_round < args.from_round:
        raise ValueError("to-round must be >= from-round")
    return SyncConfig(
        db_path=args.db,
        from_year=args.from_year,
        to_year=args.to_year,
        from_round=args.from_round,
        to_round=args.to_round,
        sleep_seconds=args.sleep_seconds,
        max_retries=args.max_retries,
        include_lap_times=not args.skip_lap_times,
        include_pit_stops=not args.skip_pit_stops,
        rebuild_features=not args.skip_feature_rebuild,
    )


def main() -> None:
    config = parse_args()
    sync_history(config)
    print(f"done: {config.db_path}")


if __name__ == "__main__":
    main()
