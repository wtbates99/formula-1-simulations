import fastf1

YEAR = 2025
GP_ROUND = 2
SESSION_INDICATOR = "R"
DRIVER = "VER"


def gtelemetry(year: int, gp_round: int, session_indicator: str, driver: str):
    session = fastf1.get_session(year, gp_round, session_indicator)
    session.load()
    laps = session.laps.pick_driver(driver)
    telemetry = laps.get_telemetry()
    return telemetry


if __name__ == "__main__":
    gtelemetry(
        year=YEAR, gp_round=GP_ROUND, session_indicator=SESSION_INDICATOR, driver=DRIVER
    )

