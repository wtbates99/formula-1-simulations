use crate::heatmap;
use crate::interpolate::Spline;
use crate::types::*;
use duckdb::Connection;
use rayon::prelude::*;
use std::collections::HashMap;

// ── Raw data structures loaded from DuckDB ──────────────────────────────────

pub struct RawSample {
    pub session_time: f64,
    pub x: f32,
    pub y: f32,
    pub speed: f32,
    pub gear: u8,
    pub throttle: f32,
    pub brake: f32,
    pub drs: i64,
}

pub struct LapRecord {
    pub lap_number: u32,
    pub lap_start_time_s: f64,
    pub position: u8,
    pub compound: String,
    pub tyre_life: u8,
}

pub struct DriverData {
    pub driver_number: String,
    pub abbreviation: String,
    pub team: String,
    pub spline_x: Spline,
    pub spline_y: Spline,
    pub samples: Vec<RawSample>,
    pub laps: Vec<LapRecord>,
}

pub struct SessionData {
    pub event_name: String,
    pub session: String,
    pub duration_s: f64,
    pub drivers: Vec<DriverData>,
    pub heatmap: Vec<HeatCell>,
    pub track_layout: TrackLayout,
}

// ── App state ────────────────────────────────────────────────────────────────

pub struct AppState {
    pub db_path: String,
    pub session: Option<SessionData>,
}

impl AppState {
    pub fn new(db_path: String) -> Self {
        AppState { db_path, session: None }
    }
}

// ── Intermediate struct for parallel spline building ─────────────────────────

struct DriverRaw {
    driver_number: String,
    abbreviation: String,
    team: String,
    samples: Vec<RawSample>,
    laps: Vec<LapRecord>,
}

// ── Session loading ──────────────────────────────────────────────────────────

pub fn load_session(
    db_path: &str,
    event_name: &str,
    session: &str,
) -> Result<SessionData, String> {
    let conn = Connection::open(db_path)
        .map_err(|e| format!("Failed to open DuckDB: {e}"))?;

    // ── 1. Query combined position + car telemetry via ASOF JOIN ─────────────
    let pos_query = "
        SELECT p.DriverNumber, p.SessionTime, p.X, p.Y,
               c.Speed, c.nGear, c.Throttle, c.Brake, c.DRS
        FROM position_telemetry p
        ASOF JOIN car_telemetry c
            ON p.DriverNumber = c.DriverNumber
            AND p.EventName = c.EventName
            AND p.Session = c.Session
            AND p.SessionTime >= c.SessionTime
        WHERE p.EventName = ? AND p.Session = ?
        AND p.X != 0 AND p.Y != 0
        ORDER BY p.DriverNumber, p.SessionTime
    ";

    let mut stmt = conn
        .prepare(pos_query)
        .map_err(|e| format!("Failed to prepare position query: {e}"))?;

    let mut driver_samples: HashMap<String, Vec<RawSample>> = HashMap::new();

    let mut rows = stmt
        .query([event_name, session])
        .map_err(|e| format!("Failed to query positions: {e}"))?;

    while let Some(row) = rows.next().map_err(|e| format!("Row error: {e}"))? {
        let driver_number: String = row.get(0).map_err(|e| format!("Col 0: {e}"))?;
        let session_time: f64 = row.get(1).map_err(|e| format!("Col 1: {e}"))?;
        let x: f64 = row.get(2).map_err(|e| format!("Col 2: {e}"))?;
        let y: f64 = row.get(3).map_err(|e| format!("Col 3: {e}"))?;
        let speed: f64 = row.get(4).unwrap_or(0.0);
        let gear: i32 = row.get(5).unwrap_or(0);
        let throttle: f64 = row.get(6).unwrap_or(0.0);
        let brake: f64 = row.get(7).unwrap_or(0.0);
        let drs: i64 = row.get(8).unwrap_or(0);

        let sample = RawSample {
            session_time,
            x: x as f32,
            y: y as f32,
            speed: speed as f32,
            gear: gear.clamp(0, 8) as u8,
            throttle: (throttle as f32).clamp(0.0, 1.0),
            brake: (brake as f32).clamp(0.0, 1.0),
            drs,
        };

        driver_samples.entry(driver_number).or_default().push(sample);
    }

    if driver_samples.is_empty() {
        return Err("No position data found for this session".to_string());
    }

    // ── 2. Query laps ─────────────────────────────────────────────────────────
    let laps_query = "
        SELECT DriverNumber, Driver, Team, LapNumber,
               COALESCE(LapStartTime, 0.0) as LapStartTime,
               COALESCE(CAST(Position AS INTEGER), 20) as Position,
               COALESCE(Compound, 'HARD') as Compound,
               COALESCE(CAST(TyreLife AS INTEGER), 0) as TyreLife
        FROM laps
        WHERE EventName = ? AND Session = ?
        ORDER BY DriverNumber, LapNumber
    ";

    let mut lap_stmt = conn
        .prepare(laps_query)
        .map_err(|e| format!("Failed to prepare laps query: {e}"))?;

    let mut driver_laps: HashMap<String, Vec<LapRecord>> = HashMap::new();
    let mut driver_abbr: HashMap<String, String> = HashMap::new();
    let mut driver_team: HashMap<String, String> = HashMap::new();

    let mut lap_rows = lap_stmt
        .query([event_name, session])
        .map_err(|e| format!("Failed to query laps: {e}"))?;

    while let Some(row) = lap_rows.next().map_err(|e| format!("Lap row error: {e}"))? {
        let driver_number: String = row.get(0).map_err(|e| format!("Lap col 0: {e}"))?;
        let driver_name: String = row.get(1).unwrap_or_default();
        let team: String = row.get(2).unwrap_or_default();
        let lap_number: i32 = row.get(3).unwrap_or(0);
        let lap_start_time: f64 = row.get(4).unwrap_or(0.0);
        let position: i32 = row.get(5).unwrap_or(20);
        let compound: String = row.get(6).unwrap_or_else(|_| "HARD".to_string());
        let tyre_life: i32 = row.get(7).unwrap_or(0);

        let abbr = abbreviate_driver_name(&driver_name, &driver_number);
        driver_abbr.entry(driver_number.clone()).or_insert(abbr);
        driver_team.entry(driver_number.clone()).or_insert(team);

        driver_laps.entry(driver_number.clone()).or_default().push(LapRecord {
            lap_number: lap_number as u32,
            lap_start_time_s: lap_start_time,
            position: position.clamp(1, 20) as u8,
            compound,
            tyre_life: tyre_life.clamp(0, 255) as u8,
        });
    }

    // ── 3. Overall session duration ───────────────────────────────────────────
    let duration_s = driver_samples
        .values()
        .flat_map(|s| s.last())
        .map(|s| s.session_time)
        .fold(0.0_f64, f64::max);

    // ── 4. Heatmap from all positions ─────────────────────────────────────────
    let all_positions: Vec<(f32, f32)> = driver_samples
        .values()
        .flat_map(|s| s.iter().map(|r| (r.x, r.y)))
        .collect();
    let all_speeds: Vec<f32> = driver_samples
        .values()
        .flat_map(|s| s.iter().map(|r| r.speed))
        .collect();
    let heatmap = heatmap::compute_heatmap(&all_positions, &all_speeds);

    // ── 5. Track layout (use driver "1" or first driver) ─────────────────────
    let ref_driver = if driver_samples.contains_key("1") {
        "1".to_string()
    } else {
        let mut keys: Vec<&String> = driver_samples.keys().collect();
        keys.sort();
        keys.first().copied().cloned().unwrap_or_default()
    };

    let track_layout = if let Some(ref_samples) = driver_samples.get(&ref_driver) {
        build_track_layout(ref_samples, duration_s)
    } else {
        TrackLayout {
            center_line: vec![],
            x_min: -7734.0,
            x_max: 3879.0,
            y_min: -1722.0,
            y_max: 17775.0,
            duration_s,
            lap_distance_m: 6201.0, // Las Vegas GP circuit length
        }
    };

    // ── 6. Build DriverRaw vec, then parallel spline construction ─────────────
    let mut driver_numbers: Vec<String> = driver_samples.keys().cloned().collect();
    driver_numbers.sort();

    // Consume the HashMap into a vec of DriverRaw for rayon
    let raw_drivers: Vec<DriverRaw> = driver_numbers
        .into_iter()
        .filter_map(|num| {
            let samples = driver_samples.remove(&num)?;
            let abbreviation = driver_abbr.get(&num).cloned().unwrap_or_else(|| num.clone());
            let team = driver_team.get(&num).cloned().unwrap_or_default();
            let laps = driver_laps.remove(&num).unwrap_or_default();
            Some(DriverRaw { driver_number: num, abbreviation, team, samples, laps })
        })
        .collect();

    // Parallel spline building
    let drivers: Vec<DriverData> = raw_drivers
        .into_par_iter()
        .map(|raw| {
            let ts: Vec<f64> = raw.samples.iter().map(|s| s.session_time).collect();
            let xs: Vec<f64> = raw.samples.iter().map(|s| s.x as f64).collect();
            let ys: Vec<f64> = raw.samples.iter().map(|s| s.y as f64).collect();

            let spline_x = Spline::new(&ts, &xs);
            let spline_y = Spline::new(&ts, &ys);

            DriverData {
                driver_number: raw.driver_number,
                abbreviation: raw.abbreviation,
                team: raw.team,
                spline_x,
                spline_y,
                samples: raw.samples,
                laps: raw.laps,
            }
        })
        .collect();

    Ok(SessionData {
        event_name: event_name.to_string(),
        session: session.to_string(),
        duration_s,
        drivers,
        heatmap,
        track_layout,
    })
}

// ── Frame computation ────────────────────────────────────────────────────────

pub fn get_frame_at(session: &SessionData, time_s: f64) -> FrameData {
    let drivers: Vec<DriverFrame> = session
        .drivers
        .iter()
        .map(|d| build_driver_frame(d, time_s))
        .collect();

    FrameData { time_s, drivers }
}

fn build_driver_frame(driver: &DriverData, time_s: f64) -> DriverFrame {
    let x = driver.spline_x.eval(time_s) as f32;
    let y = driver.spline_y.eval(time_s) as f32;

    // Heading: atan2 of the forward direction (0.1s lookahead)
    let x2 = driver.spline_x.eval(time_s + 0.1) as f32;
    let y2 = driver.spline_y.eval(time_s + 0.1) as f32;
    let heading = (y2 - y).atan2(x2 - x);

    // ASOF telemetry lookup
    let sample = asof_sample(&driver.samples, time_s);

    // ASOF lap lookup
    let lap = asof_lap(&driver.laps, time_s);

    let (speed, gear, throttle, brake, drs) = sample
        .map(|s| (s.speed, s.gear, s.throttle, s.brake, s.drs))
        .unwrap_or((0.0, 1, 0.0, 0.0, 0));

    let drs_active = matches!(drs, 10 | 12 | 14);
    let is_in_pit = speed < 20.0;

    let (position, compound, tyre_life) = lap
        .map(|l| (l.position, l.compound.clone(), l.tyre_life))
        .unwrap_or((20, "HARD".to_string(), 0));

    DriverFrame {
        driver_number: driver.driver_number.clone(),
        x,
        y,
        heading,
        speed,
        gear,
        throttle,
        brake,
        drs_active,
        position,
        compound,
        tyre_life,
        is_in_pit,
    }
}

// ── ASOF binary search helpers ───────────────────────────────────────────────

fn asof_sample(samples: &[RawSample], time_s: f64) -> Option<&RawSample> {
    if samples.is_empty() {
        return None;
    }
    let idx = samples.partition_point(|s| s.session_time <= time_s);
    if idx == 0 {
        Some(&samples[0])
    } else {
        Some(&samples[idx - 1])
    }
}

fn asof_lap(laps: &[LapRecord], time_s: f64) -> Option<&LapRecord> {
    if laps.is_empty() {
        return None;
    }
    let idx = laps.partition_point(|l| l.lap_start_time_s <= time_s);
    if idx == 0 {
        Some(&laps[0])
    } else {
        Some(&laps[idx - 1])
    }
}

// ── Track layout builder ─────────────────────────────────────────────────────

fn build_track_layout(samples: &[RawSample], duration_s: f64) -> TrackLayout {
    let center_line: Vec<[f32; 2]> = samples
        .iter()
        .enumerate()
        .filter(|(i, _)| i % 20 == 0)
        .map(|(_, s)| [s.x, s.y])
        .collect();

    let x_min = samples.iter().map(|s| s.x).fold(f32::INFINITY, f32::min);
    let x_max = samples.iter().map(|s| s.x).fold(f32::NEG_INFINITY, f32::max);
    let y_min = samples.iter().map(|s| s.y).fold(f32::INFINITY, f32::min);
    let y_max = samples.iter().map(|s| s.y).fold(f32::NEG_INFINITY, f32::max);

    // Estimate one-lap distance from arc length over first ~200 samples (single lap)
    let lap_distance_m: f32 = {
        let lap_end = samples.len().min(200);
        let mut d = 0.0_f32;
        for i in 1..lap_end {
            let dx = samples[i].x - samples[i-1].x;
            let dy = samples[i].y - samples[i-1].y;
            d += (dx*dx + dy*dy).sqrt();
        }
        d
    };

    TrackLayout { center_line, x_min, x_max, y_min, y_max, duration_s, lap_distance_m }
}

// ── Driver name abbreviation ─────────────────────────────────────────────────

fn abbreviate_driver_name(name: &str, fallback: &str) -> String {
    let parts: Vec<&str> = name.split_whitespace().collect();
    if parts.len() >= 2 {
        let last = parts.last().unwrap();
        let abbr: String = last.chars().take(3).collect::<String>().to_uppercase();
        if abbr.len() == 3 { abbr } else { fallback.to_string() }
    } else if !name.is_empty() {
        name.chars().take(3).collect::<String>().to_uppercase()
    } else {
        fallback.to_string()
    }
}
