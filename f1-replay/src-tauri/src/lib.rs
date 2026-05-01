mod commands;
mod heatmap;
mod interpolate;
mod race_analysis;
mod session;
mod simulation;
mod telemetry_analysis;
mod types;

use parking_lot::Mutex;
use session::AppState;
use std::sync::Arc;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let db_path = "/Users/willbates/code/formula-1-simulations/f1.duckdb".to_string();
    let state = Arc::new(Mutex::new(AppState::new(db_path)));

    tauri::Builder::default()
        .manage(state)
        .invoke_handler(tauri::generate_handler![
            commands::get_sessions,
            commands::load_session_cmd,
            commands::get_speed_heatmap,
            commands::get_frame,
            commands::get_driver_telemetry,
            commands::get_driver_meta,
            commands::run_simulation,
            commands::compare_drivers_cmd,
            commands::compare_laps_cmd,
            commands::get_aero_fit_cmd,
            commands::get_race_analysis,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
