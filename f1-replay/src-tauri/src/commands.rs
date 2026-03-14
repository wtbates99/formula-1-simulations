use crate::session::{load_session, get_frame_at, AppState};
use crate::simulation::{self, SimulationScenario, SimulationResult};
use crate::telemetry_analysis;
use crate::types::*;
use duckdb::Connection;
use parking_lot::Mutex;
use std::sync::Arc;
use tauri::State;

pub type AppStateHandle = Arc<Mutex<AppState>>;

// ── get_sessions ─────────────────────────────────────────────────────────────

#[tauri::command]
pub async fn get_sessions(
    state: State<'_, AppStateHandle>,
) -> Result<Vec<SessionInfo>, String> {
    let db_path = state.lock().db_path.clone();

    tokio::task::spawn_blocking(move || {
        let conn = Connection::open(&db_path)
            .map_err(|e| format!("Failed to open DuckDB: {e}"))?;

        // Try the sessions table first
        let primary_result = query_sessions_table(&conn);

        match primary_result {
            Ok(sessions) if !sessions.is_empty() => Ok(sessions),
            _ => {
                // Fall back: derive sessions from laps or position_telemetry
                query_sessions_fallback(&conn)
            }
        }
    })
    .await
    .map_err(|e| format!("Task error: {e}"))?
}

fn query_sessions_table(conn: &Connection) -> Result<Vec<SessionInfo>, String> {
    let mut stmt = conn
        .prepare(
            "SELECT DISTINCT EventName, Session, Year \
             FROM sessions \
             ORDER BY Year, EventName",
        )
        .map_err(|e| format!("Prepare sessions: {e}"))?;

    let mut sessions = Vec::new();
    let mut rows = stmt.query([]).map_err(|e| format!("Query sessions: {e}"))?;

    while let Some(row) = rows.next().map_err(|e| format!("Row: {e}"))? {
        let event_name: String = row.get(0).map_err(|e| format!("Col 0: {e}"))?;
        let session: String = row.get(1).map_err(|e| format!("Col 1: {e}"))?;
        let year: Option<i64> = row.get(2).ok();
        sessions.push(SessionInfo { event_name, session, year });
    }

    Ok(sessions)
}

fn query_sessions_fallback(conn: &Connection) -> Result<Vec<SessionInfo>, String> {
    // Try laps table first
    let laps_result = (|| {
        let mut stmt = conn
            .prepare(
                "SELECT DISTINCT EventName, Session, NULL as Year \
                 FROM laps \
                 ORDER BY EventName, Session",
            )
            .map_err(|e| format!("Prepare laps fallback: {e}"))?;

        let mut sessions = Vec::new();
        let mut rows = stmt.query([]).map_err(|e| format!("Query laps fallback: {e}"))?;

        while let Some(row) = rows.next().map_err(|e| format!("Row: {e}"))? {
            let event_name: String = row.get(0).map_err(|e| format!("Col 0: {e}"))?;
            let session: String = row.get(1).map_err(|e| format!("Col 1: {e}"))?;
            sessions.push(SessionInfo { event_name, session, year: None });
        }

        Ok::<Vec<SessionInfo>, String>(sessions)
    })();

    match laps_result {
        Ok(sessions) if !sessions.is_empty() => return Ok(sessions),
        _ => {}
    }

    // Fall back to position_telemetry
    let mut stmt = conn
        .prepare(
            "SELECT DISTINCT EventName, Session, NULL as Year \
             FROM position_telemetry \
             ORDER BY EventName, Session",
        )
        .map_err(|e| format!("Prepare pos fallback: {e}"))?;

    let mut sessions = Vec::new();
    let mut rows = stmt.query([]).map_err(|e| format!("Query pos fallback: {e}"))?;

    while let Some(row) = rows.next().map_err(|e| format!("Row: {e}"))? {
        let event_name: String = row.get(0).map_err(|e| format!("Col 0: {e}"))?;
        let session: String = row.get(1).map_err(|e| format!("Col 1: {e}"))?;
        sessions.push(SessionInfo { event_name, session, year: None });
    }

    Ok(sessions)
}

// ── load_session_cmd ─────────────────────────────────────────────────────────

#[tauri::command]
pub async fn load_session_cmd(
    event_name: String,
    session: String,
    state: State<'_, AppStateHandle>,
) -> Result<TrackLayout, String> {
    let db_path = state.lock().db_path.clone();

    let session_data = tokio::task::spawn_blocking(move || {
        load_session(&db_path, &event_name, &session)
    })
    .await
    .map_err(|e| format!("Task join error: {e}"))??;

    let layout = session_data.track_layout.clone();
    state.lock().session = Some(session_data);
    Ok(layout)
}

// ── get_speed_heatmap ─────────────────────────────────────────────────────────

#[tauri::command]
pub async fn get_speed_heatmap(
    state: State<'_, AppStateHandle>,
) -> Result<Vec<HeatCell>, String> {
    let heatmap = state
        .lock()
        .session
        .as_ref()
        .ok_or_else(|| "No session loaded".to_string())?
        .heatmap
        .clone();
    Ok(heatmap)
}

// ── get_frame ─────────────────────────────────────────────────────────────────

#[tauri::command]
pub async fn get_frame(
    time_s: f64,
    state: State<'_, AppStateHandle>,
) -> Result<FrameData, String> {
    let locked = state.lock();
    let session = locked
        .session
        .as_ref()
        .ok_or_else(|| "No session loaded".to_string())?;
    Ok(get_frame_at(session, time_s))
}

// ── get_driver_telemetry ──────────────────────────────────────────────────────

#[tauri::command]
pub async fn get_driver_telemetry(
    driver_number: String,
    time_start: f64,
    time_end: f64,
    state: State<'_, AppStateHandle>,
) -> Result<DriverTelemetry, String> {
    let locked = state.lock();
    let session = locked
        .session
        .as_ref()
        .ok_or_else(|| "No session loaded".to_string())?;

    let driver = session
        .drivers
        .iter()
        .find(|d| d.driver_number == driver_number)
        .ok_or_else(|| format!("Driver {driver_number} not found"))?;

    // Collect samples in [time_start, time_end]
    let filtered: Vec<&crate::session::RawSample> = driver
        .samples
        .iter()
        .filter(|s| s.session_time >= time_start && s.session_time <= time_end)
        .collect();

    let times: Vec<f64> = filtered.iter().map(|s| s.session_time).collect();
    let speeds: Vec<f32> = filtered.iter().map(|s| s.speed).collect();
    let gears: Vec<u8> = filtered.iter().map(|s| s.gear).collect();
    let throttles: Vec<f32> = filtered.iter().map(|s| s.throttle).collect();
    let brakes: Vec<f32> = filtered.iter().map(|s| s.brake).collect();

    Ok(DriverTelemetry { driver_number, times, speeds, gears, throttles, brakes })
}

// ── get_driver_meta ────────────────────────────────────────────────────────────

#[tauri::command]
pub async fn get_driver_meta(
    state: State<'_, AppStateHandle>,
) -> Result<Vec<DriverMeta>, String> {
    let locked = state.lock();
    let session = locked
        .session
        .as_ref()
        .ok_or_else(|| "No session loaded".to_string())?;

    let meta: Vec<DriverMeta> = session
        .drivers
        .iter()
        .map(|d| DriverMeta {
            driver_number: d.driver_number.clone(),
            abbreviation: d.abbreviation.clone(),
            team: d.team.clone(),
        })
        .collect();

    Ok(meta)
}

// ── run_simulation ────────────────────────────────────────────────────────────

#[tauri::command]
pub async fn run_simulation(
    scenario: SimulationScenario,
    state: State<'_, AppStateHandle>,
) -> Result<SimulationResult, String> {
    let locked = state.lock();
    let session = locked.session.as_ref().ok_or_else(|| "No session loaded".to_string())?;
    simulation::run_simulation(session, &scenario)
}

// ── compare_drivers_cmd ───────────────────────────────────────────────────────

#[tauri::command]
pub async fn compare_drivers_cmd(
    driver_a: String,
    driver_b: String,
    state: State<'_, AppStateHandle>,
) -> Result<DriverComparison, String> {
    let locked = state.lock();
    let session = locked.session.as_ref().ok_or_else(|| "No session loaded".to_string())?;
    simulation::compare_drivers(session, &driver_a, &driver_b)
}

// ── compare_laps_cmd ─────────────────────────────────────────────────────────

/// Distance-normalised fastest-lap comparison with per-channel telemetry overlay.
#[tauri::command]
pub async fn compare_laps_cmd(
    driver_a: String,
    driver_b: String,
    state: State<'_, AppStateHandle>,
) -> Result<LapComparison, String> {
    let locked = state.lock();
    let session = locked.session.as_ref().ok_or_else(|| "No session loaded".to_string())?;

    let a = session.drivers.iter().find(|d| d.driver_number == driver_a)
        .ok_or_else(|| format!("Driver {driver_a} not found"))?;
    let b = session.drivers.iter().find(|d| d.driver_number == driver_b)
        .ok_or_else(|| format!("Driver {driver_b} not found"))?;

    telemetry_analysis::compare_laps(a, b)
}

// ── get_aero_fit_cmd ──────────────────────────────────────────────────────────

/// Fit Cd*A from braking telemetry for a single driver.
#[tauri::command]
pub async fn get_aero_fit_cmd(
    driver_number: String,
    state: State<'_, AppStateHandle>,
) -> Result<AeroFitResult, String> {
    let locked = state.lock();
    let session = locked.session.as_ref().ok_or_else(|| "No session loaded".to_string())?;

    let driver = session.drivers.iter().find(|d| d.driver_number == driver_number)
        .ok_or_else(|| format!("Driver {driver_number} not found"))?;

    Ok(telemetry_analysis::fit_cda(driver))
}
