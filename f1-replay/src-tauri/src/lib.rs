mod types;
mod interpolate;
mod heatmap;
mod session;
mod simulation;
mod commands;

use parking_lot::Mutex;
use std::sync::Arc;
use session::AppState;

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
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
