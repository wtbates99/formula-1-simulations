#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import sqlite3
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse


DEFAULT_SIM_CONFIG: dict[str, Any] = {
    "fixed_dt": 1.0 / 240.0,
    "max_cars": 20,
    "replay_capacity_steps": 120000,
    "active_cars": 8,
}

DEFAULT_CAR_CONFIG: dict[str, Any] = {
    "mass_kg": 798.0,
    "wheelbase_m": 3.6,
    "cg_to_front_m": 1.6,
    "cg_to_rear_m": 2.0,
    "tire_radius_m": 0.34,
    "mu_long": 1.85,
    "mu_lat": 2.1,
    "cdA": 1.12,
    "clA": 3.2,
    "rolling_resistance": 180.0,
    "brake_force_max_n": 18500.0,
    "steer_gain": 0.22,
    "powertrain": {
        "gear_ratios": [3.18, 2.31, 1.79, 1.45, 1.22, 1.05, 0.92, 0.82],
        "gear_count": 8,
        "final_drive": 3.05,
        "driveline_efficiency": 0.92,
        "shift_rpm_up": 11800.0,
        "shift_rpm_down": 6200.0,
        "torque_curve": [
            [4000.0, 510.0],
            [6000.0, 640.0],
            [8000.0, 760.0],
            [9500.0, 810.0],
            [11000.0, 780.0],
            [12000.0, 730.0],
            [13000.0, 640.0],
        ],
    },
}

TIRE_MAP: dict[str, float] = {
    "hard": 0.95,
    "medium": 1.0,
    "soft": 1.06,
}

WEATHER_PRESET: dict[str, tuple[float, float]] = {
    "dry": (1.0, 1.0),
    "damp": (0.93, 1.03),
    "wet": (0.83, 1.08),
}


@dataclass(frozen=True)
class Vec3:
    x: float
    y: float
    z: float


def _parse_csv(value: str | None) -> list[str]:
    if not value:
        return []
    return [v.strip() for v in value.split(",") if v.strip()]


def _f(value: str | None, default: float) -> float:
    if value is None or value == "":
        return default
    return float(value)


def _i(value: str | None, default: int) -> int:
    if value is None or value == "":
        return default
    return int(value)


def _percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    sorted_vals = sorted(values)
    idx = int(max(0, min(len(sorted_vals) - 1, round((len(sorted_vals) - 1) * pct))))
    return float(sorted_vals[idx])


def _distance(a: Vec3, b: Vec3) -> float:
    dx = b.x - a.x
    dy = b.y - a.y
    dz = b.z - a.z
    return math.sqrt(dx * dx + dy * dy + dz * dz)


def _resample_polyline(points: list[Vec3], target_count: int) -> list[Vec3]:
    if len(points) <= target_count:
        return points

    cumulative = [0.0]
    for i in range(1, len(points)):
        cumulative.append(cumulative[-1] + _distance(points[i - 1], points[i]))
    total = cumulative[-1]
    if total <= 1e-6:
        return points[:target_count]

    out: list[Vec3] = []
    src_index = 0
    for i in range(target_count):
        t = (total * i) / max(1, target_count - 1)
        while src_index + 1 < len(cumulative) and cumulative[src_index + 1] < t:
            src_index += 1
        if src_index + 1 >= len(points):
            out.append(points[-1])
            continue
        seg_len = cumulative[src_index + 1] - cumulative[src_index]
        if seg_len <= 1e-9:
            out.append(points[src_index])
            continue
        alpha = (t - cumulative[src_index]) / seg_len
        p0 = points[src_index]
        p1 = points[src_index + 1]
        out.append(
            Vec3(
                x=p0.x + (p1.x - p0.x) * alpha,
                y=p0.y + (p1.y - p0.y) * alpha,
                z=p0.z + (p1.z - p0.z) * alpha,
            )
        )
    return out


def _extract_single_lap(points: list[Vec3]) -> list[Vec3]:
    if len(points) < 200:
        return points

    start_idx = 0
    start = points[start_idx]
    travelled = 0.0
    end_idx = len(points) - 1

    for i in range(1, len(points)):
        travelled += _distance(points[i - 1], points[i])
        if travelled < 3000.0:
            continue
        if _distance(points[i], start) < 45.0:
            end_idx = i
            break

    lap = points[start_idx : end_idx + 1]
    return lap if len(lap) >= 100 else points


def _curvature(p0: Vec3, p1: Vec3, p2: Vec3) -> float:
    a = math.hypot(p1.x - p0.x, p1.y - p0.y)
    b = math.hypot(p2.x - p1.x, p2.y - p1.y)
    c = math.hypot(p2.x - p0.x, p2.y - p0.y)
    if a <= 1e-6 or b <= 1e-6 or c <= 1e-6:
        return 0.0
    cross = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x)
    return (2.0 * cross) / (a * b * c)


def _track_from_points(event_name: str, points: list[Vec3]) -> dict[str, Any]:
    lap_points = _extract_single_lap(points)
    lap_points = _resample_polyline(lap_points, 900)
    if len(lap_points) < 3:
        raise ValueError("not enough telemetry points to build track")

    nodes: list[list[float]] = []
    s = 0.0
    z0 = lap_points[0].z
    nodes.append([0.0, 0.0, 0.0])

    for i in range(1, len(lap_points)):
        s += _distance(lap_points[i - 1], lap_points[i])
        prev_i = i - 1 if i - 1 > 0 else 0
        next_i = i + 1 if i + 1 < len(lap_points) else len(lap_points) - 1
        k = _curvature(lap_points[prev_i], lap_points[i], lap_points[next_i])
        z = lap_points[i].z - z0
        nodes.append([float(s), float(k), float(z)])

    closing = _distance(lap_points[-1], lap_points[0])
    length_m = s + closing
    if length_m > s + 1.0:
        nodes.append([float(length_m), float(nodes[0][1]), float(nodes[0][2])])

    return {
        "name": event_name,
        "length_m": float(length_m),
        "nodes": nodes,
    }


def _parse_weather(weather: str | None) -> tuple[float, float]:
    if weather is None:
        return (1.0, 1.0)
    low = weather.lower()
    return WEATHER_PRESET.get(low, (1.0, 1.0))


def _driver_list(
    con: sqlite3.Connection,
    year: int,
    round_number: int,
    session: str,
    drivers_filter: list[str],
    max_drivers: int,
) -> list[sqlite3.Row]:
    rows = con.execute(
        """
        SELECT DriverNumber, Driver, TeamName
        FROM telemetry
        WHERE Year = ? AND RoundNumber = ? AND Session = ?
        GROUP BY DriverNumber, Driver, TeamName
        ORDER BY Driver ASC
        """,
        (year, round_number, session),
    ).fetchall()
    if not rows:
        raise ValueError("no telemetry rows for selected session")

    if drivers_filter:
        wanted = {v.upper() for v in drivers_filter}
        rows = [
            r
            for r in rows
            if str(r["Driver"]).upper() in wanted or str(r["DriverNumber"]).upper() in wanted
        ]
        if not rows:
            raise ValueError("requested drivers not found in selected session")

    return rows[: max(1, min(max_drivers, 20))]


def _apply_scenario(
    car_cfg: dict[str, Any],
    track_cfg: dict[str, Any],
    weather: str | None,
    tire: str | None,
    aggression: float,
    sector_tires: list[str],
    sector_aggr: list[float],
) -> dict[str, Any]:
    weather_grip, weather_drag = _parse_weather(weather)
    tire_mul = TIRE_MAP.get((tire or "medium").lower(), 1.0)

    sector_tire_mul: list[float] = []
    for t in sector_tires[:3]:
        sector_tire_mul.append(TIRE_MAP.get(t.lower(), 1.0))
    while len(sector_tire_mul) < 3:
        sector_tire_mul.append(tire_mul)

    sector_aggr_norm = [max(0.7, min(1.5, v)) for v in (sector_aggr[:3] + [1.0, 1.0, 1.0])[:3]]

    avg_sector_tire = sum(sector_tire_mul) / 3.0
    avg_sector_aggr = sum(sector_aggr_norm) / 3.0

    total_grip_mul = weather_grip * tire_mul * avg_sector_tire
    car_cfg["mu_lat"] = float(max(1.1, min(3.2, car_cfg["mu_lat"] * total_grip_mul)))
    car_cfg["mu_long"] = float(max(0.9, min(3.0, car_cfg["mu_long"] * (weather_grip * tire_mul))))
    car_cfg["cdA"] = float(max(0.8, min(1.8, car_cfg["cdA"] * weather_drag)))
    car_cfg["brake_force_max_n"] = float(max(10000.0, min(26000.0, car_cfg["brake_force_max_n"] * (0.92 + 0.12 * aggression))))
    car_cfg["steer_gain"] = float(max(0.08, min(0.45, car_cfg["steer_gain"] * (0.9 + 0.18 * avg_sector_aggr))))

    nodes = track_cfg["nodes"]
    total_len = max(1.0, float(track_cfg["length_m"]))
    sector_len = total_len / 3.0
    for node in nodes:
        s = float(node[0])
        sector = min(2, int(s / sector_len))
        aggr = sector_aggr_norm[sector]
        tire_g = sector_tire_mul[sector]
        node[1] = float(node[1] * (1.0 + (aggr - 1.0) * 0.12) / max(0.8, tire_g))

    return {
        "weather": weather or "dry",
        "tire": tire or "medium",
        "aggression": aggression,
        "sector_tires": sector_tires[:3],
        "sector_aggression": sector_aggr_norm,
    }


def _load_bootstrap(
    db_path: Path,
    year: int | None,
    round_number: int | None,
    session: str | None,
    driver: str | None,
    drivers: list[str],
    max_drivers: int,
    weather: str | None,
    tire: str | None,
    aggression: float,
    sector_tires: list[str],
    sector_aggr: list[float],
) -> dict[str, Any]:
    with sqlite3.connect(db_path) as con:
        con.row_factory = sqlite3.Row
        con.execute("PRAGMA temp_store = MEMORY")
        chosen = con.execute(
            """
            SELECT Year, RoundNumber, Session
            FROM telemetry
            GROUP BY Year, RoundNumber, Session
            ORDER BY Year DESC, RoundNumber DESC, Session ASC
            LIMIT 1
            """
        ).fetchone()
        if chosen is None:
            raise ValueError("telemetry table has no usable data")

        selected_year = int(year if year is not None else chosen["Year"])
        selected_round = int(round_number if round_number is not None else chosen["RoundNumber"])
        selected_session = str(session if session is not None else chosen["Session"])

        requested = drivers.copy()
        if driver:
            requested.append(driver)

        available_drivers = _driver_list(
            con,
            selected_year,
            selected_round,
            selected_session,
            requested,
            max_drivers,
        )

        lead = available_drivers[0]
        selected_driver = str(lead["Driver"])
        selected_driver_number = str(lead["DriverNumber"])

        fastest_lap = con.execute(
            """
            SELECT LapStartDate, LapTimeSeconds
            FROM lap_features
            WHERE Year = ? AND RoundNumber = ? AND Session = ? AND DriverNumber = ?
              AND LapTimeSeconds IS NOT NULL
              AND IsAccurate = 1
              AND LapStartDate IS NOT NULL
            ORDER BY LapTimeSeconds ASC
            LIMIT 1
            """,
            (selected_year, selected_round, selected_session, selected_driver_number),
        ).fetchone()

        lap_start: dt.datetime | None = None
        lap_end: dt.datetime | None = None
        if fastest_lap is not None:
            lap_start_raw = str(fastest_lap["LapStartDate"])
            lap_time_s = float(fastest_lap["LapTimeSeconds"])
            try:
                lap_start = dt.datetime.fromisoformat(lap_start_raw.replace("Z", "+00:00"))
                lap_end = lap_start + dt.timedelta(seconds=lap_time_s + 2.0)
            except ValueError:
                lap_start = None
                lap_end = None

        rows = con.execute(
            """
            SELECT Date, X, Y, Z, Speed, RPM, nGear
            FROM telemetry
            WHERE Year = ? AND RoundNumber = ? AND Session = ? AND DriverNumber = ?
              AND X IS NOT NULL AND Y IS NOT NULL
            """,
            (selected_year, selected_round, selected_session, selected_driver_number),
        ).fetchall()
        if not rows:
            raise ValueError("telemetry table has no coordinate rows for selected driver/session")
        rows = sorted(rows, key=lambda r: str(r["Date"]))

        points: list[Vec3] = []
        speeds_mps: list[float] = []
        rpms: list[float] = []
        gears: list[int] = []
        last_point: Vec3 | None = None

        for row in rows:
            if lap_start is not None and lap_end is not None and row["Date"] is not None:
                row_date_raw = str(row["Date"])
                try:
                    row_dt = dt.datetime.fromisoformat(row_date_raw.replace(" ", "T").replace("Z", "+00:00"))
                except ValueError:
                    row_dt = None
                if row_dt is not None and (row_dt < lap_start or row_dt > lap_end):
                    continue

            p = Vec3(
                x=float(row["X"]) * 0.1,
                y=float(row["Y"]) * 0.1,
                z=float(row["Z"] if row["Z"] is not None else 0.0) * 0.1,
            )
            if last_point is not None and _distance(last_point, p) < 0.05:
                continue
            points.append(p)
            last_point = p

            speed_kph = row["Speed"]
            if speed_kph is not None:
                speeds_mps.append(float(speed_kph) / 3.6)
            if row["RPM"] is not None:
                rpms.append(float(row["RPM"]))
            if row["nGear"] is not None:
                gears.append(int(row["nGear"]))

        if len(points) < 80:
            raise ValueError("not enough usable telemetry points after dedup")

        event_name_row = con.execute(
            """
            SELECT EventName
            FROM sessions
            WHERE Year = ? AND RoundNumber = ? AND Session = ?
            LIMIT 1
            """,
            (selected_year, selected_round, selected_session),
        ).fetchone()
        event_name = (
            str(event_name_row["EventName"])
            if event_name_row is not None and event_name_row["EventName"]
            else f"Y{selected_year} R{selected_round} {selected_session}"
        )

        track_cfg = _track_from_points(event_name=event_name, points=points)

        car_cfg = json.loads(json.dumps(DEFAULT_CAR_CONFIG))
        if rpms:
            shift_up = max(8500.0, min(13000.0, _percentile(rpms, 0.95)))
            car_cfg["powertrain"]["shift_rpm_up"] = shift_up
            car_cfg["powertrain"]["shift_rpm_down"] = max(4500.0, shift_up * 0.55)
        if gears:
            gear_count = max(1, min(8, max(gears)))
            car_cfg["powertrain"]["gear_count"] = gear_count
        if speeds_mps:
            vmax = _percentile(speeds_mps, 0.99)
            car_cfg["clA"] = max(2.2, min(4.8, 2.4 + vmax / 50.0))
            car_cfg["cdA"] = max(0.9, min(1.4, 1.5 - min(vmax, 105.0) / 300.0))

        scenario = _apply_scenario(
            car_cfg=car_cfg,
            track_cfg=track_cfg,
            weather=weather,
            tire=tire,
            aggression=aggression,
            sector_tires=sector_tires,
            sector_aggr=sector_aggr,
        )

        sim_cfg = json.loads(json.dumps(DEFAULT_SIM_CONFIG))
        sim_cfg["active_cars"] = max(1, min(len(available_drivers), int(sim_cfg["max_cars"])))

        return {
            "sim": sim_cfg,
            "car": car_cfg,
            "track": track_cfg,
            "meta": {
                "year": selected_year,
                "round": selected_round,
                "session": selected_session,
                "driver": selected_driver,
                "driver_number": selected_driver_number,
                "event_name": event_name,
                "selected_drivers": [
                    {
                        "driver": str(r["Driver"]),
                        "driver_number": str(r["DriverNumber"]),
                        "team": str(r["TeamName"] if r["TeamName"] is not None else ""),
                    }
                    for r in available_drivers
                ],
                "points_used": len(points),
                "scenario": scenario,
            },
        }


def _catalog(db_path: Path, year: int | None, round_number: int | None, session: str | None) -> dict[str, Any]:
    with sqlite3.connect(db_path) as con:
        con.row_factory = sqlite3.Row
        con.execute("PRAGMA temp_store = MEMORY")
        if year is not None and round_number is not None and session is not None:
            drivers = con.execute(
                """
                SELECT DriverNumber, Driver, TeamName, COUNT(*) AS samples
                FROM telemetry
                WHERE Year = ? AND RoundNumber = ? AND Session = ?
                GROUP BY DriverNumber, Driver, TeamName
                ORDER BY Driver ASC
                """,
                (year, round_number, session),
            ).fetchall()
            return {
                "year": year,
                "round": round_number,
                "session": session,
                "drivers": [
                    {
                        "driver": str(r["Driver"]),
                        "driver_number": str(r["DriverNumber"]),
                        "team": str(r["TeamName"] if r["TeamName"] is not None else ""),
                        "samples": int(r["samples"]),
                    }
                    for r in drivers
                ],
            }

        sessions = con.execute(
            """
            SELECT
              t.Year,
              t.RoundNumber,
              t.Session,
              '' AS EventName,
              COUNT(DISTINCT t.DriverNumber) AS driver_count,
              COUNT(*) AS telemetry_rows
            FROM telemetry t
            GROUP BY t.Year, t.RoundNumber, t.Session
            ORDER BY t.Year DESC, t.RoundNumber ASC, t.Session ASC
            """
        ).fetchall()
        return {
            "sessions": [
                {
                    "year": int(r["Year"]),
                    "round": int(r["RoundNumber"]),
                    "session": str(r["Session"]),
                    "event_name": str(r["EventName"]),
                    "driver_count": int(r["driver_count"]),
                    "telemetry_rows": int(r["telemetry_rows"]),
                }
                for r in sessions
            ]
        }


def _replay_data(
    db_path: Path,
    year: int,
    round_number: int,
    session: str,
    drivers: list[str],
    max_drivers: int,
    stride: int,
) -> dict[str, Any]:
    with sqlite3.connect(db_path) as con:
        con.row_factory = sqlite3.Row
        con.execute("PRAGMA temp_store = MEMORY")
        selected = _driver_list(con, year, round_number, session, drivers, max_drivers)

        traces: list[dict[str, Any]] = []
        frame_count = 0

        for d in selected:
            driver_number = str(d["DriverNumber"])
            rows = con.execute(
                """
                SELECT Date, X, Y, Speed, RPM, nGear, Throttle, Brake
                FROM telemetry
                WHERE Year = ? AND RoundNumber = ? AND Session = ? AND DriverNumber = ?
                  AND X IS NOT NULL AND Y IS NOT NULL
                """,
                (year, round_number, session, driver_number),
            ).fetchall()
            rows = sorted(rows, key=lambda r: str(r["Date"]))
            if stride > 1:
                rows = rows[::stride]

            x: list[float] = []
            y: list[float] = []
            speed: list[float] = []
            rpm: list[float] = []
            gear: list[int] = []
            throttle: list[float] = []
            brake: list[float] = []

            for r in rows:
                x.append(float(r["X"]) * 0.1)
                y.append(float(r["Y"]) * 0.1)
                speed.append(float(r["Speed"] if r["Speed"] is not None else 0.0) / 3.6)
                rpm.append(float(r["RPM"] if r["RPM"] is not None else 0.0))
                gear.append(int(r["nGear"] if r["nGear"] is not None else 0))
                throttle.append(float(r["Throttle"] if r["Throttle"] is not None else 0.0) / 100.0)
                brake.append(float(r["Brake"] if r["Brake"] is not None else 0.0))

            frame_count = len(x) if frame_count == 0 else min(frame_count, len(x))
            traces.append(
                {
                    "driver": str(d["Driver"]),
                    "driver_number": driver_number,
                    "team": str(d["TeamName"] if d["TeamName"] is not None else ""),
                    "x": x,
                    "y": y,
                    "speed": speed,
                    "rpm": rpm,
                    "gear": gear,
                    "throttle": throttle,
                    "brake": brake,
                }
            )

        event = con.execute(
            """
            SELECT EventName
            FROM sessions
            WHERE Year = ? AND RoundNumber = ? AND Session = ?
            LIMIT 1
            """,
            (year, round_number, session),
        ).fetchone()

        return {
            "meta": {
                "year": year,
                "round": round_number,
                "session": session,
                "event_name": str(event["EventName"] if event is not None else f"Y{year} R{round_number}"),
                "driver_count": len(traces),
                "frame_count": frame_count,
                "stride": max(1, stride),
            },
            "traces": [
                {
                    **t,
                    "x": t["x"][:frame_count],
                    "y": t["y"][:frame_count],
                    "speed": t["speed"][:frame_count],
                    "rpm": t["rpm"][:frame_count],
                    "gear": t["gear"][:frame_count],
                    "throttle": t["throttle"][:frame_count],
                    "brake": t["brake"][:frame_count],
                }
                for t in traces
            ],
        }


def _benchmark_data(
    db_path: Path,
    year: int,
    round_number: int,
    session: str,
    drivers: list[str],
) -> dict[str, Any]:
    with sqlite3.connect(db_path) as con:
        con.row_factory = sqlite3.Row
        con.execute("PRAGMA temp_store = MEMORY")

        rows = con.execute(
            """
            SELECT Driver, DriverNumber, LapTimeSeconds
            FROM lap_features
            WHERE Year = ? AND RoundNumber = ? AND Session = ?
              AND LapTimeSeconds IS NOT NULL
              AND IsAccurate = 1
            ORDER BY LapTimeSeconds ASC
            """,
            (year, round_number, session),
        ).fetchall()

        if drivers:
            wanted = {v.upper() for v in drivers}
            rows = [
                r
                for r in rows
                if str(r["Driver"]).upper() in wanted or str(r["DriverNumber"]).upper() in wanted
            ]

        if not rows:
            return {
                "year": year,
                "round": round_number,
                "session": session,
                "fastest_lap_s": 0.0,
                "fastest_driver": "",
                "top_laps": [],
            }

        best_per_driver: dict[str, dict[str, Any]] = {}
        for r in rows:
            key = str(r["DriverNumber"])
            lap = float(r["LapTimeSeconds"])
            if key not in best_per_driver or lap < float(best_per_driver[key]["lap_time_s"]):
                best_per_driver[key] = {
                    "driver": str(r["Driver"]),
                    "driver_number": key,
                    "lap_time_s": lap,
                }

        top = sorted(best_per_driver.values(), key=lambda x: float(x["lap_time_s"]))[:10]
        return {
            "year": year,
            "round": round_number,
            "session": session,
            "fastest_lap_s": float(top[0]["lap_time_s"]),
            "fastest_driver": str(top[0]["driver"]),
            "top_laps": top,
        }


class ApiHandler(BaseHTTPRequestHandler):
    db_path: Path

    def do_OPTIONS(self) -> None:  # noqa: N802
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        qs = parse_qs(parsed.query)

        if parsed.path == "/api/health":
            self._write_json(200, {"ok": True})
            return

        try:
            if parsed.path == "/api/catalog":
                year = int(qs["year"][0]) if "year" in qs and qs["year"] else None
                round_number = int(qs["round"][0]) if "round" in qs and qs["round"] else None
                session = str(qs["session"][0]) if "session" in qs and qs["session"] else None
                self._write_json(200, _catalog(self.db_path, year, round_number, session))
                return

            if parsed.path == "/api/replay":
                year = int(qs["year"][0])
                round_number = int(qs["round"][0])
                session = str(qs["session"][0])
                drivers = _parse_csv(qs.get("drivers", [None])[0])
                max_drivers = _i(qs.get("max_drivers", [None])[0], 20)
                stride = _i(qs.get("stride", [None])[0], 8)
                payload = _replay_data(
                    db_path=self.db_path,
                    year=year,
                    round_number=round_number,
                    session=session,
                    drivers=drivers,
                    max_drivers=max_drivers,
                    stride=max(1, stride),
                )
                self._write_json(200, payload)
                return

            if parsed.path == "/api/benchmark":
                year = int(qs["year"][0])
                round_number = int(qs["round"][0])
                session = str(qs["session"][0])
                drivers = _parse_csv(qs.get("drivers", [None])[0])
                payload = _benchmark_data(
                    db_path=self.db_path,
                    year=year,
                    round_number=round_number,
                    session=session,
                    drivers=drivers,
                )
                self._write_json(200, payload)
                return

            if parsed.path == "/api/bootstrap-config":
                year = int(qs["year"][0]) if "year" in qs and qs["year"] else None
                round_number = int(qs["round"][0]) if "round" in qs and qs["round"] else None
                session = str(qs["session"][0]) if "session" in qs and qs["session"] else None
                driver = str(qs["driver"][0]) if "driver" in qs and qs["driver"] else None
                drivers = _parse_csv(qs.get("drivers", [None])[0])
                max_drivers = _i(qs.get("max_drivers", [None])[0], 20)
                weather = str(qs["weather"][0]) if "weather" in qs and qs["weather"] else None
                tire = str(qs["tire"][0]) if "tire" in qs and qs["tire"] else None
                aggression = _f(qs.get("aggression", [None])[0], 1.0)
                sector_tires = _parse_csv(qs.get("sector_tires", [None])[0])
                sector_aggr = [_f(v, 1.0) for v in _parse_csv(qs.get("sector_aggr", [None])[0])]

                payload = _load_bootstrap(
                    db_path=self.db_path,
                    year=year,
                    round_number=round_number,
                    session=session,
                    driver=driver,
                    drivers=drivers,
                    max_drivers=max_drivers,
                    weather=weather,
                    tire=tire,
                    aggression=max(0.7, min(1.5, aggression)),
                    sector_tires=sector_tires,
                    sector_aggr=sector_aggr,
                )
                self._write_json(200, payload)
                return

            self._write_json(404, {"error": "not_found"})
        except Exception as exc:  # noqa: BLE001
            self._write_json(500, {"error": "request_failed", "detail": str(exc)})

    def log_message(self, format: str, *args: Any) -> None:
        return

    def _write_json(self, status: int, body: dict[str, Any]) -> None:
        encoded = json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(encoded)


def main() -> None:
    parser = argparse.ArgumentParser(description="Serve simulation bootstrap and replay data from SQLite.")
    parser.add_argument("--db-path", default="f1.sqlite", help="Path to SQLite database")
    parser.add_argument("--host", default="127.0.0.1", help="Bind host")
    parser.add_argument("--port", type=int, default=8000, help="Bind port")
    args = parser.parse_args()

    db_path = Path(args.db_path).resolve()
    if not db_path.exists():
        raise SystemExit(f"DB not found: {db_path}")

    class BoundHandler(ApiHandler):
        pass

    BoundHandler.db_path = db_path
    server = ThreadingHTTPServer((args.host, args.port), BoundHandler)
    print(f"SQLite API listening on http://{args.host}:{args.port} (db={db_path})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
