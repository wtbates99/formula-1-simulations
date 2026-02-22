from __future__ import annotations

import argparse
import sqlite3
from collections import defaultdict
from typing import Literal

import fastf1
import pandas as pd

YEAR: int = 2025
GP_ROUND: int | None = None
SESSION_INDICATOR: Literal["FP1", "FP2", "FP3", "Q", "R"] | None = None
DB_PATH = "f1.sqlite"
SESSION_TYPES: tuple[Literal["FP1", "FP2", "FP3", "Q", "R"], ...] = (
    "FP1",
    "FP2",
    "FP3",
    "Q",
    "R",
)


def _normalize_for_sqlite(df: pd.DataFrame) -> pd.DataFrame:
    out = df.copy()
    for col in out.columns:
        series = out[col]
        if pd.api.types.is_timedelta64_dtype(series):
            out[col] = series.dt.total_seconds()
            continue
        if pd.api.types.is_datetime64_any_dtype(series):
            out[col] = series.dt.strftime("%Y-%m-%dT%H:%M:%S.%f%z")
            continue
        if pd.api.types.is_bool_dtype(series):
            out[col] = series.astype("Int64")
            continue
        if series.dtype == "object":
            out[col] = series.map(
                lambda value: (
                    value.total_seconds()
                    if isinstance(value, pd.Timedelta)
                    else value.isoformat()
                    if isinstance(value, pd.Timestamp)
                    else None
                    if pd.isna(value)
                    else value
                )
            )
    return out


def _write_tables_to_sqlite(tables: dict[str, pd.DataFrame], db_path: str = DB_PATH) -> None:
    with sqlite3.connect(db_path) as con:
        for table_name, dataframe in tables.items():
            _normalize_for_sqlite(dataframe).to_sql(
                table_name,
                con,
                if_exists="replace",
                index=False,
            )


def _build_lap_features(session_laps: pd.DataFrame) -> pd.DataFrame:
    if session_laps.empty:
        return pd.DataFrame()
    lap_features = session_laps.copy()
    lap_features["LapTimeSeconds"] = lap_features["LapTime"].dt.total_seconds()
    lap_features["Sector1Seconds"] = lap_features["Sector1Time"].dt.total_seconds()
    lap_features["Sector2Seconds"] = lap_features["Sector2Time"].dt.total_seconds()
    lap_features["Sector3Seconds"] = lap_features["Sector3Time"].dt.total_seconds()
    lap_features["IsPitInLap"] = lap_features["PitInTime"].notna().astype(int)
    lap_features["IsPitOutLap"] = lap_features["PitOutTime"].notna().astype(int)
    lap_features["IsQuickLap"] = (
        lap_features["IsAccurate"].fillna(False)
        & lap_features["LapTimeSeconds"].notna()
        & (lap_features["LapTimeSeconds"] < lap_features["LapTimeSeconds"].median())
    ).astype(int)
    return lap_features


def _build_driver_stints(session_laps: pd.DataFrame) -> pd.DataFrame:
    if session_laps.empty:
        return pd.DataFrame()
    stint_source = session_laps.dropna(subset=["DriverNumber", "Stint"]).copy()
    if stint_source.empty:
        return pd.DataFrame()
    stint_source["LapTimeSeconds"] = stint_source["LapTime"].dt.total_seconds()
    stints = (
        stint_source.groupby(
            ["DriverNumber", "Driver", "Team", "Stint", "Compound", "FreshTyre"],
            dropna=False,
        )
        .agg(
            StartLap=("LapNumber", "min"),
            EndLap=("LapNumber", "max"),
            Laps=("LapNumber", "count"),
            MeanLapTimeSeconds=("LapTimeSeconds", "mean"),
            MedianLapTimeSeconds=("LapTimeSeconds", "median"),
            BestLapTimeSeconds=("LapTimeSeconds", "min"),
        )
        .reset_index()
    )
    return stints


def ingest_fastf1_data(
    year: int,
    gp_round: int | None = None,
    session_indicator: Literal["FP1", "FP2", "FP3", "Q", "R"] | None = None,
) -> dict[str, pd.DataFrame]:
    schedule = fastf1.get_event_schedule(year, include_testing=False).copy()
    rounds = (
        [int(gp_round)]
        if gp_round is not None
        else schedule["RoundNumber"].dropna().astype(int).tolist()
    )
    session_types = (
        [session_indicator] if session_indicator is not None else list(SESSION_TYPES)
    )

    tables: dict[str, list[pd.DataFrame]] = defaultdict(list)
    schedule["Year"] = year
    tables["event_schedule"].append(schedule)

    for round_number in rounds:
        for session_type in session_types:
            try:
                session = fastf1.get_session(year, round_number, session_type)
                session.load(laps=True, telemetry=True, weather=True, messages=True)
            except Exception as error:
                print(
                    f"Skipping {year} R{round_number} {session_type}: {error}"
                )
                continue

            context = {
                "Year": year,
                "RoundNumber": int(round_number),
                "Session": session_type,
                "EventName": session.event.EventName,
                "EventDate": session.event.EventDate,
                "Country": session.event.Country,
                "Location": session.event.Location,
            }

            session_meta = pd.DataFrame(
                [
                    {
                        **context,
                        "SessionName": session.name,
                        "ApiPath": getattr(session, "api_path", ""),
                        "F1ApiSupport": int(bool(session.f1_api_support)),
                        "Date": session.date,
                    }
                ]
            )
            tables["sessions"].append(session_meta)

            if not session.results.empty:
                results = session.results.copy()
                for k, v in context.items():
                    results[k] = v
                tables["session_results"].append(results)

            if not session.laps.empty:
                laps = session.laps.copy()
                for k, v in context.items():
                    laps[k] = v
                tables["laps"].append(laps)
                tables["lap_features"].append(_build_lap_features(laps))
                tables["driver_stints"].append(_build_driver_stints(laps))

            if not session.weather_data.empty:
                weather = session.weather_data.copy()
                for k, v in context.items():
                    weather[k] = v
                tables["weather"].append(weather)

            if not session.track_status.empty:
                track_status = session.track_status.copy()
                for k, v in context.items():
                    track_status[k] = v
                tables["track_status"].append(track_status)

            if not session.session_status.empty:
                session_status = session.session_status.copy()
                for k, v in context.items():
                    session_status[k] = v
                tables["session_status"].append(session_status)

            if not session.race_control_messages.empty:
                race_control = session.race_control_messages.copy()
                for k, v in context.items():
                    race_control[k] = v
                tables["race_control_messages"].append(race_control)

            for driver_number, car_df in session.car_data.items():
                if car_df.empty:
                    continue
                car_data = car_df.copy()
                car_data["DriverNumber"] = str(driver_number)
                for k, v in context.items():
                    car_data[k] = v
                tables["car_telemetry"].append(car_data)

            for driver_number, pos_df in session.pos_data.items():
                if pos_df.empty:
                    continue
                position_data = pos_df.copy()
                position_data["DriverNumber"] = str(driver_number)
                for k, v in context.items():
                    position_data[k] = v
                tables["position_telemetry"].append(position_data)

    return {
        table_name: pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()
        for table_name, frames in tables.items()
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Ingest FastF1 data into SQLite for analysis, visualization, and simulation."
    )
    parser.add_argument("--year", type=int, default=YEAR, help="Season year (e.g. 2025)")
    parser.add_argument(
        "--round",
        dest="gp_round",
        type=int,
        default=GP_ROUND,
        help="Race round number (omit for all rounds)",
    )
    parser.add_argument(
        "--session",
        dest="session_indicator",
        choices=SESSION_TYPES,
        default=SESSION_INDICATOR,
        help="Session code to ingest",
    )
    parser.add_argument(
        "--all-sessions",
        action="store_true",
        help="Ignore --session and ingest FP1/FP2/FP3/Q/R",
    )
    parser.add_argument("--db-path", default=DB_PATH, help="SQLite database path")
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    session_indicator = None if args.all_sessions else args.session_indicator
    data = ingest_fastf1_data(
        year=args.year,
        gp_round=args.gp_round,
        session_indicator=session_indicator,
    )
    _write_tables_to_sqlite(data, db_path=args.db_path)
    print("Ingestion complete.")
    for table_name, df in data.items():
        print(f"{table_name}: {len(df):,} rows")
