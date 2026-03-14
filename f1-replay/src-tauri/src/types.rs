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
