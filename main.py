from typing import Literal
import fastf1
import pandas as pd

YEAR: int = 2025
GP_ROUND: int | None = 3
SESSION_INDICATOR: Literal["FP1", "FP2", "FP3", "Q", "R"] | None = "R"
DRIVER: str | None = "HAM"
SESSION_TYPES: tuple[Literal["FP1", "FP2", "FP3", "Q", "R"], ...] = (
    "FP1",
    "FP2",
    "FP3",
    "Q",
    "R",
)


def laps_data(
    year: int,
    gp_round: int | None = None,
    session_indicator: Literal["FP1", "FP2", "FP3", "Q", "R"] | None = None,
    driver: str | None = None,
) -> pd.DataFrame:
    """Return concatenated lap telemetry filtered by optional F1 selectors.

    Args:
        year: Season year (for example, 2025). Required.
        gp_round: Optional grand prix round number (for example, 3).
            If omitted, all rounds in the year are used.
        session_indicator: Optional session code: "FP1", "FP2", "FP3", "Q", or "R".
            If omitted, all supported sessions are used.
        driver: Optional driver selector (for example, "HAM" or "44").
            If omitted, all drivers in each selected session are used.

    Returns:
        A single DataFrame containing appended telemetry for all matching laps.
        Extra context columns are included: "Year", "RoundNumber", "Session", and "Driver".
        If no data matches, returns an empty DataFrame.

    Usage:
        - Year only (everything in season):
            laps_data(2025)
        - Year + round:
            laps_data(2025, gp_round=3)
        - Year + round + session:
            laps_data(2025, gp_round=3, session_indicator="R")
        - Year + round + session + driver:
            laps_data(2025, gp_round=3, session_indicator="R", driver="HAM")
    """
    schedule = fastf1.get_event_schedule(year)
    rounds = [gp_round] if gp_round is not None else schedule["RoundNumber"].dropna().astype(int).unique().tolist()
    session_types = [session_indicator] if session_indicator is not None else list(SESSION_TYPES)
    frames: list[pd.DataFrame] = []

    for round_number in rounds:
        for session_type in session_types:
            try:
                session = fastf1.get_session(year, int(round_number), session_type)
                session.load()
            except Exception:
                continue

            driver_results = (
                session.results[["DriverNumber", "Abbreviation", "TeamName"]]
                .dropna(subset=["DriverNumber", "Abbreviation"])
                .astype({"DriverNumber": str, "Abbreviation": str, "TeamName": str})
            )
            driver_map: dict[str, dict[str, str]] = {}
            for _, row in driver_results.iterrows():
                meta = {
                    "DriverNumber": row["DriverNumber"],
                    "Driver": row["Abbreviation"],
                    "TeamName": row["TeamName"],
                }
                driver_map[row["DriverNumber"]] = meta
                driver_map[row["Abbreviation"]] = meta

            drivers = [driver] if driver is not None else list(session.drivers)
            for driver_selector in drivers:
                try:
                    selector = str(driver_selector)
                    laps = session.laps.pick_drivers(selector)
                    if laps.empty:
                        continue
                    telemetry = laps.get_telemetry()
                    if telemetry.empty:
                        continue
                    meta = driver_map.get(selector, {"DriverNumber": selector, "Driver": selector, "TeamName": ""})
                    telemetry["Year"] = year
                    telemetry["RoundNumber"] = int(round_number)
                    telemetry["Session"] = session_type
                    telemetry["DriverNumber"] = meta["DriverNumber"]
                    telemetry["Driver"] = meta["Driver"]
                    telemetry["TeamName"] = meta["TeamName"]
                    frames.append(telemetry)
                except Exception:
                    continue

    if not frames:
        return pd.DataFrame()
    return pd.concat(frames, ignore_index=True)


if __name__ == "__main__":
    #telemetry = laps_data(YEAR, GP_ROUND, SESSION_INDICATOR, DRIVER)
    telemetry = laps_data(YEAR, GP_ROUND, SESSION_INDICATOR)
    telemetry.to_csv("telemetry.csv", index=False)
    print(telemetry.head())
