use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SessionInfo {
    pub event_name: String,
    pub session: String,
    pub year: Option<i64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DriverFrame {
    pub driver_number: String,
    pub x: f32,
    pub y: f32,
    pub heading: f32,
    pub speed: f32,
    pub gear: u8,
    pub throttle: f32,
    pub brake: f32,
    pub drs_active: bool,
    pub position: u8,
    pub compound: String,
    pub tyre_life: u8,
    pub is_in_pit: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FrameData {
    pub time_s: f64,
    pub drivers: Vec<DriverFrame>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HeatCell {
    pub x: f32,
    pub y: f32,
    pub speed_norm: f32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TrackLayout {
    pub center_line: Vec<[f32; 2]>,
    pub x_min: f32,
    pub x_max: f32,
    pub y_min: f32,
    pub y_max: f32,
    pub duration_s: f64,
    pub lap_distance_m: f32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DriverTelemetry {
    pub driver_number: String,
    pub times: Vec<f64>,
    pub speeds: Vec<f32>,
    pub gears: Vec<u8>,
    pub throttles: Vec<f32>,
    pub brakes: Vec<f32>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DriverMeta {
    pub driver_number: String,
    pub abbreviation: String,
    pub team: String,
}

// ── Distance-normalized telemetry ────────────────────────────────────────────

/// One lap's telemetry resampled at uniform 10-metre distance intervals
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LapTelemetry {
    pub driver_number: String,
    pub lap_number: u32,
    pub lap_time_s: f64,
    /// Distance axis: 0.0, 10.0, 20.0, … metres around lap
    pub distances: Vec<f32>,
    pub speeds: Vec<f32>,
    pub throttles: Vec<f32>,
    pub brakes: Vec<f32>,
    pub gears: Vec<u8>,
    pub drs: Vec<bool>,
}

/// Distance-normalised comparison of two drivers over a single reference lap
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LapComparison {
    pub driver_a: String,
    pub driver_b: String,
    pub lap_number_a: u32,
    pub lap_number_b: u32,
    pub lap_time_a: f64,
    pub lap_time_b: f64,
    pub lap_time_delta: f64,
    /// Shared distance axis (metres)
    pub distances: Vec<f32>,
    pub speeds_a: Vec<f32>,
    pub speeds_b: Vec<f32>,
    pub throttles_a: Vec<f32>,
    pub throttles_b: Vec<f32>,
    pub brakes_a: Vec<f32>,
    pub brakes_b: Vec<f32>,
    pub gears_a: Vec<u8>,
    pub gears_b: Vec<u8>,
    /// time_a[i] − time_b[i] at each distance: positive means A is ahead at that point
    pub delta_time: Vec<f32>,
    pub mini_sectors: Vec<MiniSector>,
}

/// 25-metre mini sector dominance
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MiniSector {
    pub distance_start: f32,
    pub distance_end: f32,
    /// Seconds: positive = A faster, negative = B faster
    pub delta_s: f32,
}

/// Aero fit result from Cd*A regression on braking telemetry
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AeroFitResult {
    pub driver_number: String,
    /// Combined drag coefficient × frontal area (m²)
    pub cda: f64,
    /// Rolling resistance coefficient (dimensionless)
    pub c_roll: f64,
    /// R² of fit
    pub r_squared: f64,
    /// Number of braking samples used
    pub sample_count: usize,
}

/// Full-session driver comparison (lap-level)
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DriverComparison {
    pub driver_a: String,
    pub driver_b: String,
    pub fastest_lap_a: f64,
    pub fastest_lap_b: f64,
    pub avg_lap_a: f64,
    pub avg_lap_b: f64,
    pub max_speed_a: f32,
    pub max_speed_b: f32,
    pub lap_deltas: Vec<LapDelta>,
    pub summary: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LapDelta {
    pub lap_number: u32,
    pub time_a: f64,
    pub time_b: f64,
    pub delta_s: f64,
    pub cumulative_delta_s: f64,
}

// ── Race analysis engine ────────────────────────────────────────────────────

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RaceAnalysis {
    pub event_name: String,
    pub session: String,
    pub driver_count: usize,
    pub valid_lap_count: usize,
    pub fastest_driver: Option<String>,
    pub fastest_lap_s: Option<f64>,
    pub median_race_pace_s: Option<f64>,
    pub drivers: Vec<DriverAnalysis>,
    pub stints: Vec<StintAnalysis>,
    pub insights: Vec<RaceInsight>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DriverAnalysis {
    pub driver_number: String,
    pub abbreviation: String,
    pub team: String,
    pub final_position: u8,
    pub positions_gained: i16,
    pub valid_laps: usize,
    pub fastest_lap_s: Option<f64>,
    pub median_lap_s: Option<f64>,
    pub consistency_s: Option<f64>,
    pub max_speed_kmh: f32,
    pub avg_speed_kmh: f32,
    pub avg_throttle_pct: f32,
    pub avg_brake_pct: f32,
    pub drs_usage_pct: f32,
    pub pit_laps: Vec<u32>,
    pub performance_score: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StintAnalysis {
    pub driver_number: String,
    pub start_lap: u32,
    pub end_lap: u32,
    pub compound: String,
    pub laps: usize,
    pub avg_lap_s: Option<f64>,
    pub tyre_life_start: u8,
    pub tyre_life_end: u8,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RaceInsight {
    pub kind: String,
    pub title: String,
    pub detail: String,
    pub driver_number: Option<String>,
    pub severity: f32,
}
