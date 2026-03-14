/// Distance-normalised telemetry analysis and Cd*A aero fitting.
use crate::session::{DriverData, RawSample};
use crate::types::{AeroFitResult, LapComparison, LapTelemetry, MiniSector};

// ── Constants ─────────────────────────────────────────────────────────────────

const AIR_DENSITY: f64 = 1.225; // kg/m³ at sea level, 15 °C
const SAMPLE_STEP: f32 = 10.0;  // metres between resampled points
const MINI_SECTOR_LEN: f32 = 25.0; // metres per mini-sector
const CAR_MASS_KG: f64 = 798.0; // F1 car + driver minimum

// ── Arc-length distance computation ──────────────────────────────────────────

/// Compute cumulative arc-length distance (metres) for each raw sample.
/// Returns a Vec the same length as `samples` with monotonically increasing
/// distances starting at 0. Uses XY position deltas only.
pub fn compute_distances(samples: &[RawSample]) -> Vec<f32> {
    let mut dists = Vec::with_capacity(samples.len());
    if samples.is_empty() {
        return dists;
    }
    dists.push(0.0_f32);
    for i in 1..samples.len() {
        let dx = samples[i].x - samples[i - 1].x;
        let dy = samples[i].y - samples[i - 1].y;
        let d = (dx * dx + dy * dy).sqrt();
        dists.push(dists[i - 1] + d);
    }
    dists
}

// ── Single-lap distance-normalised telemetry ─────────────────────────────────

/// Build a `LapTelemetry` for driver `d` on lap `lap_number`.
///
/// We locate the raw samples that fall within the lap's time window
/// [lap_start, next_lap_start), compute arc-length distances, then
/// resample all channels onto a uniform `SAMPLE_STEP`-metre grid.
pub fn build_lap_telemetry(
    driver: &DriverData,
    lap_number: u32,
) -> Option<LapTelemetry> {
    // Find the lap record
    let lap_idx = driver.laps.iter().position(|l| l.lap_number == lap_number)?;
    let lap = &driver.laps[lap_idx];
    let t_start = lap.lap_start_time_s;

    // Next lap start (or very large) gives the upper bound
    let t_end = driver
        .laps
        .get(lap_idx + 1)
        .map(|l| l.lap_start_time_s)
        .unwrap_or(f64::MAX);

    let lap_time = if t_end < f64::MAX { t_end - t_start } else { return None; };
    if lap_time < 60.0 || lap_time > 200.0 {
        // Implausible lap – skip (SC, inlap, outlap, etc.)
        return None;
    }

    // Slice samples belonging to this lap
    let samples: Vec<&RawSample> = driver
        .samples
        .iter()
        .filter(|s| s.session_time >= t_start && s.session_time < t_end)
        .collect();

    if samples.len() < 10 {
        return None;
    }

    // ── Arc-length on the slice ─────────────────────────────────────────────
    let mut arc_dists: Vec<f32> = vec![0.0];
    for i in 1..samples.len() {
        let dx = samples[i].x - samples[i - 1].x;
        let dy = samples[i].y - samples[i - 1].y;
        arc_dists.push(arc_dists[i - 1] + (dx * dx + dy * dy).sqrt());
    }
    let total_dist = *arc_dists.last().unwrap();
    if total_dist < 100.0 {
        return None;
    }

    // ── Resample onto uniform grid ──────────────────────────────────────────
    let n_steps = (total_dist / SAMPLE_STEP).ceil() as usize;
    let mut distances = Vec::with_capacity(n_steps);
    let mut speeds    = Vec::with_capacity(n_steps);
    let mut throttles = Vec::with_capacity(n_steps);
    let mut brakes    = Vec::with_capacity(n_steps);
    let mut gears     = Vec::with_capacity(n_steps);
    let mut drs_out   = Vec::with_capacity(n_steps);

    let mut j = 0usize; // pointer into arc_dists / samples
    for k in 0..n_steps {
        let target_d = k as f32 * SAMPLE_STEP;
        distances.push(target_d);

        // Advance j until arc_dists[j+1] > target_d (or we reach end)
        while j + 1 < arc_dists.len() - 1 && arc_dists[j + 1] <= target_d {
            j += 1;
        }

        if j + 1 >= samples.len() {
            // Clamp to last sample
            let s = samples[samples.len() - 1];
            speeds.push(s.speed);
            throttles.push(s.throttle);
            brakes.push(s.brake);
            gears.push(s.gear);
            drs_out.push(matches!(s.drs, 10 | 12 | 14));
            continue;
        }

        let d0 = arc_dists[j];
        let d1 = arc_dists[j + 1];
        let frac = if (d1 - d0).abs() < 1e-6 { 0.0_f32 } else { (target_d - d0) / (d1 - d0) };
        let frac = frac.clamp(0.0, 1.0);
        let s0 = samples[j];
        let s1 = samples[j + 1];

        speeds.push(s0.speed + (s1.speed - s0.speed) * frac);
        throttles.push(s0.throttle + (s1.throttle - s0.throttle) * frac);
        brakes.push(s0.brake + (s1.brake - s0.brake) * frac);
        // Gear: nearest neighbour
        gears.push(if frac < 0.5 { s0.gear } else { s1.gear });
        drs_out.push(matches!(if frac < 0.5 { s0.drs } else { s1.drs }, 10 | 12 | 14));
    }

    Some(LapTelemetry {
        driver_number: driver.driver_number.clone(),
        lap_number,
        lap_time_s: lap_time,
        distances,
        speeds,
        throttles,
        brakes,
        gears,
        drs: drs_out,
    })
}

// ── Fastest qualifying lap selection ─────────────────────────────────────────

/// Return the lap telemetry for the single fastest valid lap of this driver.
pub fn fastest_lap_telemetry(driver: &DriverData) -> Option<LapTelemetry> {
    let mut best: Option<LapTelemetry> = None;
    for lap in &driver.laps {
        if let Some(lt) = build_lap_telemetry(driver, lap.lap_number) {
            let is_better = best.as_ref().map_or(true, |b| lt.lap_time_s < b.lap_time_s);
            if is_better {
                best = Some(lt);
            }
        }
    }
    best
}

// ── Distance-normalised two-driver lap comparison ────────────────────────────

/// Compare two drivers on their respective fastest valid laps.
/// Outputs a `LapComparison` with all channels aligned on a shared distance
/// axis and a running delta_time strip.
pub fn compare_laps(
    driver_a: &DriverData,
    driver_b: &DriverData,
) -> Result<LapComparison, String> {
    let lt_a = fastest_lap_telemetry(driver_a)
        .ok_or_else(|| format!("No valid lap found for driver {}", driver_a.driver_number))?;
    let lt_b = fastest_lap_telemetry(driver_b)
        .ok_or_else(|| format!("No valid lap found for driver {}", driver_b.driver_number))?;

    // Use the shorter distance axis as the reference
    let n = lt_a.distances.len().min(lt_b.distances.len());
    let distances: Vec<f32> = (0..n).map(|i| lt_a.distances[i]).collect();

    // Build cumulative-time axis for each driver (time elapsed to reach each distance)
    let time_a = build_time_axis(&lt_a, n);
    let time_b = build_time_axis(&lt_b, n);

    // Delta: positive means A is ahead in time at this point (A got here earlier)
    let delta_time: Vec<f32> = (0..n).map(|i| time_a[i] - time_b[i]).collect();

    // Mini-sectors: every MINI_SECTOR_LEN metres
    let mini_sectors = build_mini_sectors(&distances, &delta_time);

    Ok(LapComparison {
        driver_a: driver_a.driver_number.clone(),
        driver_b: driver_b.driver_number.clone(),
        lap_number_a: lt_a.lap_number,
        lap_number_b: lt_b.lap_number,
        lap_time_a: lt_a.lap_time_s,
        lap_time_b: lt_b.lap_time_s,
        lap_time_delta: lt_a.lap_time_s - lt_b.lap_time_s,
        distances,
        speeds_a: lt_a.speeds[..n].to_vec(),
        speeds_b: lt_b.speeds[..n].to_vec(),
        throttles_a: lt_a.throttles[..n].to_vec(),
        throttles_b: lt_b.throttles[..n].to_vec(),
        brakes_a: lt_a.brakes[..n].to_vec(),
        brakes_b: lt_b.brakes[..n].to_vec(),
        gears_a: lt_a.gears[..n].to_vec(),
        gears_b: lt_b.gears[..n].to_vec(),
        delta_time,
        mini_sectors,
    })
}

/// Compute cumulative time (seconds) to reach each distance index.
///
/// We integrate speed (km/h → m/s) over distance steps.
fn build_time_axis(lt: &LapTelemetry, n: usize) -> Vec<f32> {
    let mut time = vec![0.0_f32; n];
    for i in 1..n {
        let avg_speed_ms = ((lt.speeds[i - 1] + lt.speeds[i]) / 2.0) / 3.6;
        let dd = lt.distances[i] - lt.distances[i - 1];
        let dt = if avg_speed_ms > 0.5 { dd / avg_speed_ms } else { 0.1 };
        time[i] = time[i - 1] + dt;
    }
    // Scale so that time[n-1] matches the actual lap time
    let scale = lt.lap_time_s as f32 / time[n - 1].max(1e-6);
    for t in time.iter_mut() {
        *t *= scale;
    }
    time
}

fn build_mini_sectors(
    distances: &[f32],
    delta_time: &[f32],
) -> Vec<MiniSector> {
    if distances.is_empty() {
        return vec![];
    }
    let total = *distances.last().unwrap();
    let n_ms = (total / MINI_SECTOR_LEN).ceil() as usize;
    let mut sectors = Vec::with_capacity(n_ms);

    let mut j = 0usize;
    for k in 0..n_ms {
        let d_start = k as f32 * MINI_SECTOR_LEN;
        let d_end   = ((k + 1) as f32 * MINI_SECTOR_LEN).min(total);

        // Accumulate delta_time samples in [d_start, d_end)
        let mut sum = 0.0_f32;
        let mut count = 0usize;
        while j < distances.len() && distances[j] < d_end {
            if distances[j] >= d_start {
                sum += delta_time[j];
                count += 1;
            }
            j += 1;
        }
        // Back up j to handle overlap between sectors
        if j > 0 && j < distances.len() && distances[j] >= d_end {
            // keep j where it is — next sector starts here
        }

        let avg_delta = if count > 0 { sum / count as f32 } else { 0.0 };
        sectors.push(MiniSector { distance_start: d_start, distance_end: d_end, delta_s: avg_delta });
    }

    sectors
}

// ── Cd*A fitting from braking telemetry ──────────────────────────────────────

/// Estimate the car's aerodynamic drag coefficient × frontal area (Cd*A) using
/// linear least-squares on deceleration samples during hard braking phases.
///
/// Physical model during braking (no driving force):
///   m * a = − (ρ/2) * CdA * v² − m * g * C_roll
///
/// Dividing by m:
///   a/m = −(ρ/(2m)) * CdA * v² − g * C_roll
///
/// Let y = −a (positive deceleration), x = v²:
///   y = α * x + β
///   α = (ρ/2m) * CdA  →  CdA = α * (2m/ρ)
///   β = g * C_roll     →  C_roll = β / g
pub fn fit_cda(driver: &DriverData) -> AeroFitResult {
    let samples = &driver.samples;
    let n = samples.len();
    if n < 50 {
        return zero_fit(&driver.driver_number);
    }

    // Collect braking samples: brake > 0.7 AND speed > 100 km/h
    let mut xs: Vec<f64> = Vec::new(); // v²  (m/s)²
    let mut ys: Vec<f64> = Vec::new(); // -a  (m/s²)

    for i in 1..n {
        let s0 = &samples[i - 1];
        let s1 = &samples[i];
        if s0.brake < 0.7 || s0.speed < 100.0 { continue; }

        let dt = (s1.session_time - s0.session_time) as f64;
        if dt < 1e-4 || dt > 0.5 { continue; }

        let v0 = s0.speed as f64 / 3.6; // km/h → m/s
        let v1 = s1.speed as f64 / 3.6;
        let accel = (v1 - v0) / dt; // negative during braking
        if accel >= 0.0 { continue; } // filter out noise / throttle application

        xs.push(v0 * v0);
        ys.push(-accel);
    }

    if xs.len() < 20 {
        return zero_fit(&driver.driver_number);
    }

    // Ordinary least-squares: y = α*x + β
    let (alpha, beta, r2) = ols_linear(&xs, &ys);
    if alpha <= 0.0 {
        return zero_fit(&driver.driver_number);
    }

    let cda = alpha * (2.0 * CAR_MASS_KG / AIR_DENSITY);
    let c_roll = beta / 9.81;

    AeroFitResult {
        driver_number: driver.driver_number.clone(),
        cda: cda.max(0.0),
        c_roll: c_roll.clamp(0.005, 0.05),
        r_squared: r2.clamp(0.0, 1.0),
        sample_count: xs.len(),
    }
}

fn zero_fit(driver_number: &str) -> AeroFitResult {
    AeroFitResult {
        driver_number: driver_number.to_string(),
        cda: 1.0,
        c_roll: 0.015,
        r_squared: 0.0,
        sample_count: 0,
    }
}

/// Returns (slope α, intercept β, R²)
fn ols_linear(xs: &[f64], ys: &[f64]) -> (f64, f64, f64) {
    let n = xs.len() as f64;
    let sx: f64 = xs.iter().sum();
    let sy: f64 = ys.iter().sum();
    let sxx: f64 = xs.iter().map(|x| x * x).sum();
    let sxy: f64 = xs.iter().zip(ys.iter()).map(|(x, y)| x * y).sum();

    let denom = n * sxx - sx * sx;
    if denom.abs() < 1e-12 {
        return (0.0, sy / n, 0.0);
    }

    let alpha = (n * sxy - sx * sy) / denom;
    let beta  = (sy - alpha * sx) / n;

    // R²
    let y_bar = sy / n;
    let ss_tot: f64 = ys.iter().map(|y| (y - y_bar).powi(2)).sum();
    let ss_res: f64 = xs.iter().zip(ys.iter()).map(|(x, y)| (y - (alpha * x + beta)).powi(2)).sum();
    let r2 = if ss_tot < 1e-12 { 0.0 } else { 1.0 - ss_res / ss_tot };

    (alpha, beta, r2)
}

// ── Unit tests ────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    fn make_sample(t: f64, x: f32, y: f32, speed: f32, brake: f32) -> RawSample {
        RawSample { session_time: t, x, y, speed, gear: 5, throttle: 0.0, brake, drs: 0 }
    }

    #[test]
    fn test_compute_distances_straight() {
        // 3 samples 100 m apart in X
        let samples = vec![
            make_sample(0.0, 0.0, 0.0, 50.0, 0.0),
            make_sample(1.0, 100.0, 0.0, 50.0, 0.0),
            make_sample(2.0, 200.0, 0.0, 50.0, 0.0),
        ];
        let d = compute_distances(&samples);
        assert_eq!(d.len(), 3);
        assert!((d[0] - 0.0).abs() < 1e-4);
        assert!((d[1] - 100.0).abs() < 1e-4);
        assert!((d[2] - 200.0).abs() < 1e-4);
    }

    #[test]
    fn test_compute_distances_diagonal() {
        let samples = vec![
            make_sample(0.0, 0.0, 0.0, 50.0, 0.0),
            make_sample(1.0, 3.0, 4.0, 50.0, 0.0), // 5 m hypotenuse
        ];
        let d = compute_distances(&samples);
        assert!((d[1] - 5.0).abs() < 1e-3);
    }

    #[test]
    fn test_compute_distances_empty() {
        assert_eq!(compute_distances(&[]).len(), 0);
    }

    #[test]
    fn test_ols_linear_perfect() {
        // y = 2x + 1
        let xs: Vec<f64> = (0..10).map(|i| i as f64).collect();
        let ys: Vec<f64> = xs.iter().map(|x| 2.0 * x + 1.0).collect();
        let (alpha, beta, r2) = ols_linear(&xs, &ys);
        assert!((alpha - 2.0).abs() < 1e-9);
        assert!((beta  - 1.0).abs() < 1e-9);
        assert!((r2    - 1.0).abs() < 1e-9);
    }

    #[test]
    fn test_ols_linear_flat() {
        let xs: Vec<f64> = vec![1.0, 2.0, 3.0];
        let ys: Vec<f64> = vec![5.0, 5.0, 5.0];
        let (alpha, _beta, _r2) = ols_linear(&xs, &ys);
        assert!(alpha.abs() < 1e-9);
    }

    #[test]
    fn test_build_mini_sectors_empty() {
        assert_eq!(build_mini_sectors(&[], &[]).len(), 0);
    }

    #[test]
    fn test_build_mini_sectors_single() {
        let d = vec![0.0_f32, 12.5, 25.0];
        let dt = vec![0.01_f32, -0.02, 0.03];
        let ms = build_mini_sectors(&d, &dt);
        assert_eq!(ms.len(), 1);
        assert!((ms[0].distance_start - 0.0).abs() < 1e-4);
        assert!((ms[0].distance_end - 25.0).abs() < 1e-4);
    }

    #[test]
    fn test_build_time_axis_scaling() {
        // Constant 100 km/h = 27.78 m/s over 100 m → ~3.6 s
        let lt = LapTelemetry {
            driver_number: "1".to_string(),
            lap_number: 1,
            lap_time_s: 90.0,
            distances: vec![0.0, 10.0, 20.0, 30.0],
            speeds:    vec![100.0; 4],
            throttles: vec![0.0; 4],
            brakes:    vec![0.0; 4],
            gears:     vec![5; 4],
            drs:       vec![false; 4],
        };
        let ta = build_time_axis(&lt, 4);
        // Must be scaled: last element should equal lap_time_s
        assert!((ta[3] - 90.0_f32).abs() < 1e-3);
        // Monotonically increasing
        for i in 1..ta.len() {
            assert!(ta[i] >= ta[i - 1]);
        }
    }
}
