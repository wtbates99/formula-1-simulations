//! F1 performance simulation engine.
//!
//! Models how changes to car parameters, driver swaps, and weather
//! affect lap times and race outcomes.

use crate::session::{DriverData, SessionData};
use serde::{Deserialize, Serialize};

// ── Parameter types ───────────────────────────────────────────────────────────

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CarParams {
    /// Multiplicative factor on engine power output (0.5–1.5, 1.0 = baseline)
    pub engine_power_factor: f64,
    /// Aero downforce coefficient (0.5–1.5, higher = more grip but more drag)
    pub aero_downforce_factor: f64,
    /// Drag reduction multiplier (0.5–1.5, lower = less drag = faster straights)
    pub aero_drag_factor: f64,
    /// Tyre compound override (None = use race data)
    pub tyre_compound: Option<String>,
    /// Tyre degradation rate multiplier (1.0 = normal)
    pub tyre_wear_rate: f64,
    /// Fuel load at race start in kg (typical: 90–105 kg)
    pub fuel_load_kg: f64,
}

impl Default for CarParams {
    fn default() -> Self {
        CarParams {
            engine_power_factor: 1.0,
            aero_downforce_factor: 1.0,
            aero_drag_factor: 1.0,
            tyre_compound: None,
            tyre_wear_rate: 1.0,
            fuel_load_kg: 95.0,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct EnvironmentParams {
    /// Track temperature in Celsius (affects tyre performance)
    pub track_temp_c: f64,
    /// Air temperature in Celsius
    pub air_temp_c: f64,
    /// Wind speed in m/s
    pub wind_speed_ms: f64,
    /// Wind direction in degrees (0 = tailwind on main straight, 180 = headwind)
    pub wind_direction_deg: f64,
    /// Humidity 0.0–1.0
    pub humidity: f64,
}

impl Default for EnvironmentParams {
    fn default() -> Self {
        EnvironmentParams {
            track_temp_c: 35.0,
            air_temp_c: 24.0,
            wind_speed_ms: 0.0,
            wind_direction_deg: 0.0,
            humidity: 0.4,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SimulationScenario {
    pub event_name: String,
    pub session: String,
    /// Which driver's setup to simulate (the "base" driver)
    pub base_driver: String,
    /// Optional: swap car with another driver's car
    pub swap_car_with: Option<String>,
    /// Optional: swap driver inputs with another driver
    pub swap_driver_inputs_with: Option<String>,
    pub car_params: CarParams,
    pub env_params: EnvironmentParams,
    /// Number of laps to simulate (None = full race)
    pub num_laps: Option<u32>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SimulatedLap {
    pub lap_number: u32,
    pub lap_time_s: f64,
    pub sector1_s: f64,
    pub sector2_s: f64,
    pub sector3_s: f64,
    pub max_speed_kmh: f64,
    pub avg_speed_kmh: f64,
    pub tyre_life: u32,
    pub compound: String,
    pub fuel_remaining_kg: f64,
    pub delta_to_baseline_s: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SimulationResult {
    pub scenario: SimulationScenario,
    pub laps: Vec<SimulatedLap>,
    pub total_time_s: f64,
    pub fastest_lap_s: f64,
    pub avg_lap_time_s: f64,
    /// Summary of what changed vs baseline
    pub delta_summary: DeltaSummary,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeltaSummary {
    pub total_time_delta_s: f64,
    pub avg_lap_delta_s: f64,
    pub straight_speed_delta_kmh: f64,
    pub corner_speed_delta_kmh: f64,
    pub description: String,
}

// ── Performance model ─────────────────────────────────────────────────────────

/// Core performance model: given car and environment parameters,
/// compute multiplicative factors on lap time components.
struct PerfFactors {
    straight_speed_factor: f64,   // > 1 = faster on straights
    corner_speed_factor: f64,     // > 1 = faster in corners
    overall_lap_factor: f64,      // combined lap time multiplier
}

fn compute_perf_factors(
    car: &CarParams,
    env: &EnvironmentParams,
    _baseline_env: &EnvironmentParams,
) -> PerfFactors {
    // Engine power: directly affects straight-line speed
    // A 10% power increase ≈ 5% straight speed increase (drag limited)
    let straight_speed_factor = car.engine_power_factor.powf(0.5)
        / car.aero_drag_factor.powf(0.3);

    // Aero downforce: affects corner speed (downforce^0.5 ≈ speed in corners)
    let corner_speed_factor = car.aero_downforce_factor.powf(0.5)
        / car.aero_drag_factor.powf(0.1);

    // Tyre grip: compound-based multiplier
    let tyre_factor = compound_grip_factor(car.tyre_compound.as_deref().unwrap_or("HARD"));

    // Fuel weight penalty: each additional kg of fuel costs ~0.03s/lap
    let fuel_penalty_per_kg = 0.03 / 90.0; // seconds per kg above baseline
    let fuel_factor = 1.0 + (car.fuel_load_kg - 95.0) * fuel_penalty_per_kg;

    // Temperature effects on tyre performance
    let ideal_track_temp = 45.0;
    let temp_diff = (env.track_temp_c - ideal_track_temp).abs();
    let temp_factor = 1.0 + temp_diff * 0.001;

    // Wind effect: drag depends on wind direction relative to track
    // Las Vegas main straight ~ north-south; simplify to sine factor
    let wind_drag_factor = 1.0
        + (env.wind_speed_ms / 100.0)
            * env.wind_direction_deg.to_radians().cos().abs()
            * 0.5;

    // Combined lap time factor (lower = faster)
    // Lap time is dominated by ~40% straights + 60% corners
    let straight_contribution = 0.40 / straight_speed_factor;
    let corner_contribution = 0.60 / corner_speed_factor;
    let base_factor = straight_contribution + corner_contribution;

    let overall_lap_factor = base_factor * tyre_factor * fuel_factor * temp_factor * wind_drag_factor;

    PerfFactors {
        straight_speed_factor,
        corner_speed_factor,
        overall_lap_factor,
    }
}

fn compound_grip_factor(compound: &str) -> f64 {
    match compound {
        "SOFT"   => 0.986,  // fastest but degrades
        "MEDIUM" => 0.993,
        "HARD"   => 1.000,  // baseline
        "INTER"  => 1.020,  // only good in wet
        "WET"    => 1.060,  // very slow in dry
        _        => 1.000,
    }
}

fn tyre_degradation_per_lap(compound: &str, wear_rate: f64) -> f64 {
    // Time loss per lap due to tyre wear (seconds)
    let base = match compound {
        "SOFT"   => 0.08,  // fastest degradation
        "MEDIUM" => 0.04,
        "HARD"   => 0.02,
        _        => 0.03,
    };
    base * wear_rate
}

// ── Main simulation function ──────────────────────────────────────────────────

pub fn run_simulation(
    session: &SessionData,
    scenario: &SimulationScenario,
) -> Result<SimulationResult, String> {
    // Get base driver data
    let base_driver = session.drivers.iter()
        .find(|d| d.driver_number == scenario.base_driver)
        .ok_or_else(|| format!("Driver {} not found", scenario.base_driver))?;

    // Get reference car driver (may be swapped)
    let car_driver = if let Some(ref swap_num) = scenario.swap_car_with {
        session.drivers.iter()
            .find(|d| d.driver_number == *swap_num)
            .unwrap_or(base_driver)
    } else {
        base_driver
    };

    // Get reference input driver (driving style, may be swapped)
    let input_driver = if let Some(ref swap_num) = scenario.swap_driver_inputs_with {
        session.drivers.iter()
            .find(|d| d.driver_number == *swap_num)
            .unwrap_or(base_driver)
    } else {
        base_driver
    };

    // Compute baseline lap times from actual race data
    let baseline_laps = extract_lap_times(base_driver);
    if baseline_laps.is_empty() {
        return Err("No lap time data for driver".to_string());
    }

    // Compute performance factors
    let baseline_env = EnvironmentParams::default();
    let factors = compute_perf_factors(&scenario.car_params, &scenario.env_params, &baseline_env);

    // Compute car efficiency delta from swapping
    let car_delta = if scenario.swap_car_with.is_some() {
        compute_car_delta(base_driver, car_driver)
    } else {
        1.0
    };

    // Compute driver style delta from swapping
    let driver_delta = if scenario.swap_driver_inputs_with.is_some() {
        compute_driver_style_delta(base_driver, input_driver)
    } else {
        1.0
    };

    let num_laps = scenario.num_laps.unwrap_or(baseline_laps.len() as u32) as usize;
    let num_laps = num_laps.min(baseline_laps.len());

    let compound = scenario.car_params.tyre_compound.clone()
        .unwrap_or_else(|| base_driver.laps.first()
            .map(|l| l.compound.clone())
            .unwrap_or_else(|| "HARD".to_string()));

    let mut simulated_laps = Vec::with_capacity(num_laps);
    let mut fuel_kg = scenario.car_params.fuel_load_kg;
    let fuel_burn_per_lap = 3.0; // ~3 kg per lap typical F1

    for (i, &baseline_time) in baseline_laps.iter().take(num_laps).enumerate() {
        let tyre_life = (i as u32 + 1) * scenario.car_params.tyre_wear_rate as u32;
        let tyre_deg = tyre_degradation_per_lap(&compound, scenario.car_params.tyre_wear_rate)
            * i as f64;

        // Combined lap time
        let simulated_time = baseline_time
            * factors.overall_lap_factor
            * car_delta
            * driver_delta
            + tyre_deg;

        let delta = simulated_time - baseline_time;

        // Sector splits (approximate 20% / 40% / 40% for Las Vegas)
        let sector1 = simulated_time * 0.20;
        let sector2 = simulated_time * 0.40;
        let sector3 = simulated_time * 0.40;

        // Speed estimates
        let base_speed = extract_avg_speed(base_driver);
        let max_speed = base_speed * factors.straight_speed_factor * 1.3;
        let avg_speed = base_speed * (0.4 * factors.straight_speed_factor + 0.6 * factors.corner_speed_factor);

        fuel_kg = (fuel_kg - fuel_burn_per_lap).max(0.0);

        simulated_laps.push(SimulatedLap {
            lap_number: i as u32 + 1,
            lap_time_s: simulated_time,
            sector1_s: sector1,
            sector2_s: sector2,
            sector3_s: sector3,
            max_speed_kmh: max_speed,
            avg_speed_kmh: avg_speed,
            tyre_life,
            compound: compound.clone(),
            fuel_remaining_kg: fuel_kg,
            delta_to_baseline_s: delta,
        });
    }

    let total_time_s: f64 = simulated_laps.iter().map(|l| l.lap_time_s).sum();
    let fastest_lap_s = simulated_laps.iter().map(|l| l.lap_time_s).fold(f64::INFINITY, f64::min);
    let avg_lap_time_s = total_time_s / simulated_laps.len() as f64;

    let baseline_total: f64 = baseline_laps.iter().take(num_laps).sum();
    let total_time_delta_s = total_time_s - baseline_total;
    let avg_lap_delta_s = total_time_delta_s / num_laps as f64;

    let base_speed = extract_avg_speed(base_driver);
    let straight_speed_delta = base_speed * 1.3 * (factors.straight_speed_factor - 1.0);
    let corner_speed_delta = base_speed * (factors.corner_speed_factor - 1.0);

    let description = build_description(scenario, &factors, total_time_delta_s, car_delta, driver_delta);

    let delta_summary = DeltaSummary {
        total_time_delta_s,
        avg_lap_delta_s,
        straight_speed_delta_kmh: straight_speed_delta,
        corner_speed_delta_kmh: corner_speed_delta,
        description,
    };

    Ok(SimulationResult {
        scenario: scenario.clone(),
        laps: simulated_laps,
        total_time_s,
        fastest_lap_s,
        avg_lap_time_s,
        delta_summary,
    })
}

// ── Compare two drivers in same session ──────────────────────────────────────

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DriverComparison {
    pub driver_a: String,
    pub driver_b: String,
    pub lap_deltas: Vec<LapDelta>,
    pub fastest_lap_a: f64,
    pub fastest_lap_b: f64,
    pub avg_lap_a: f64,
    pub avg_lap_b: f64,
    pub max_speed_a: f64,
    pub max_speed_b: f64,
    pub summary: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LapDelta {
    pub lap_number: u32,
    pub time_a: f64,
    pub time_b: f64,
    pub delta_s: f64,   // negative = A is faster
    pub cumulative_delta_s: f64,
}

pub fn compare_drivers(
    session: &SessionData,
    driver_a_num: &str,
    driver_b_num: &str,
) -> Result<DriverComparison, String> {
    let a = session.drivers.iter().find(|d| d.driver_number == driver_a_num)
        .ok_or_else(|| format!("Driver {driver_a_num} not found"))?;
    let b = session.drivers.iter().find(|d| d.driver_number == driver_b_num)
        .ok_or_else(|| format!("Driver {driver_b_num} not found"))?;

    let times_a = extract_lap_times(a);
    let times_b = extract_lap_times(b);

    let min_laps = times_a.len().min(times_b.len());
    let mut lap_deltas = Vec::with_capacity(min_laps);
    let mut cumulative = 0.0;

    for i in 0..min_laps {
        let delta = times_a[i] - times_b[i];
        cumulative += delta;
        let lap_num = a.laps.get(i).map(|l| l.lap_number).unwrap_or(i as u32 + 1);
        lap_deltas.push(LapDelta {
            lap_number: lap_num,
            time_a: times_a[i],
            time_b: times_b[i],
            delta_s: delta,
            cumulative_delta_s: cumulative,
        });
    }

    let fastest_a = times_a.iter().cloned().fold(f64::INFINITY, f64::min);
    let fastest_b = times_b.iter().cloned().fold(f64::INFINITY, f64::min);
    let avg_a = times_a.iter().sum::<f64>() / times_a.len().max(1) as f64;
    let avg_b = times_b.iter().sum::<f64>() / times_b.len().max(1) as f64;
    let max_speed_a = extract_max_speed(a);
    let max_speed_b = extract_max_speed(b);

    let winner = if fastest_a < fastest_b { &a.abbreviation } else { &b.abbreviation };
    let gap = (fastest_a - fastest_b).abs();
    let summary = format!(
        "{} has the faster lap by {:.3}s. Avg lap delta: {:.3}s",
        winner, gap, (avg_a - avg_b).abs()
    );

    Ok(DriverComparison {
        driver_a: driver_a_num.to_string(),
        driver_b: driver_b_num.to_string(),
        lap_deltas,
        fastest_lap_a: fastest_a,
        fastest_lap_b: fastest_b,
        avg_lap_a: avg_a,
        avg_lap_b: avg_b,
        max_speed_a,
        max_speed_b,
        summary,
    })
}

// ── Helper functions ──────────────────────────────────────────────────────────

fn extract_lap_times(driver: &DriverData) -> Vec<f64> {
    // Compute lap times from lap start times
    let mut times = Vec::new();
    for i in 0..driver.laps.len().saturating_sub(1) {
        let dt = driver.laps[i + 1].lap_start_time_s - driver.laps[i].lap_start_time_s;
        if dt > 60.0 && dt < 200.0 {  // reasonable F1 lap time range
            times.push(dt);
        }
    }
    times
}

fn extract_avg_speed(driver: &DriverData) -> f64 {
    if driver.samples.is_empty() { return 200.0; }
    let sum: f32 = driver.samples.iter()
        .filter(|s| s.speed > 0.0)
        .map(|s| s.speed)
        .sum();
    let count = driver.samples.iter().filter(|s| s.speed > 0.0).count();
    if count == 0 { 200.0 } else { sum as f64 / count as f64 }
}

fn extract_max_speed(driver: &DriverData) -> f64 {
    driver.samples.iter().map(|s| s.speed as f64).fold(0.0, f64::max)
}

fn compute_car_delta(base: &DriverData, swap: &DriverData) -> f64 {
    // Estimate car performance delta from max speed and average speed difference
    let base_max = extract_max_speed(base);
    let swap_max = extract_max_speed(swap);
    let base_avg = extract_avg_speed(base);
    let swap_avg = extract_avg_speed(swap);

    if base_max == 0.0 || swap_max == 0.0 { return 1.0; }

    // Car performance ≈ weighted average of straight and corner speed ratios
    let straight_ratio = base_max / swap_max.max(1.0);
    let corner_ratio = base_avg / swap_avg.max(1.0);

    // Overall time ratio: straights 40%, corners 60%
    0.4 * straight_ratio + 0.6 * corner_ratio
}

fn compute_driver_style_delta(base: &DriverData, swap: &DriverData) -> f64 {
    // Estimate driver style impact from throttle application and braking patterns
    let base_throttle_avg: f32 = if !base.samples.is_empty() {
        base.samples.iter().map(|s| s.throttle).sum::<f32>() / base.samples.len() as f32
    } else { 0.7 };

    let swap_throttle_avg: f32 = if !swap.samples.is_empty() {
        swap.samples.iter().map(|s| s.throttle).sum::<f32>() / swap.samples.len() as f32
    } else { 0.7 };

    // More aggressive throttle → faster (small effect)
    let throttle_factor = (base_throttle_avg / swap_throttle_avg.max(0.01)) as f64;
    // Clamp to reasonable range
    throttle_factor.clamp(0.95, 1.05)
}

fn build_description(
    scenario: &SimulationScenario,
    _factors: &PerfFactors,
    total_delta: f64,
    _car_delta: f64,
    _driver_delta: f64,
) -> String {
    let mut parts = vec![];

    if (scenario.car_params.engine_power_factor - 1.0).abs() > 0.01 {
        let pct = (scenario.car_params.engine_power_factor - 1.0) * 100.0;
        parts.push(format!("{:+.0}% engine power", pct));
    }
    if (scenario.car_params.aero_downforce_factor - 1.0).abs() > 0.01 {
        let pct = (scenario.car_params.aero_downforce_factor - 1.0) * 100.0;
        parts.push(format!("{:+.0}% downforce", pct));
    }
    if (scenario.car_params.aero_drag_factor - 1.0).abs() > 0.01 {
        let pct = (scenario.car_params.aero_drag_factor - 1.0) * 100.0;
        parts.push(format!("{:+.0}% drag", pct));
    }
    if let Some(ref tyre) = scenario.car_params.tyre_compound {
        parts.push(format!("{} tyres", tyre));
    }
    if let Some(ref swap) = scenario.swap_car_with {
        parts.push(format!("driving {}'s car", swap));
    }
    if let Some(ref swap) = scenario.swap_driver_inputs_with {
        parts.push(format!("with {}'s driving style", swap));
    }
    if scenario.env_params.wind_speed_ms > 1.0 {
        parts.push(format!("{:.0} m/s wind", scenario.env_params.wind_speed_ms));
    }

    let change_str = if parts.is_empty() { "baseline".to_string() } else { parts.join(", ") };
    let sign = if total_delta >= 0.0 { "+" } else { "" };
    format!("Scenario: {}. Total race time delta: {}{:.1}s", change_str, sign, total_delta)
}

// ── Tests ─────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_car_params_default() {
        let p = CarParams::default();
        assert_eq!(p.engine_power_factor, 1.0);
        assert_eq!(p.aero_downforce_factor, 1.0);
        assert_eq!(p.aero_drag_factor, 1.0);
    }

    #[test]
    fn test_compound_grip_factors_ordered() {
        // SOFT should be faster (lower factor) than MEDIUM, HARD
        let soft = compound_grip_factor("SOFT");
        let medium = compound_grip_factor("MEDIUM");
        let hard = compound_grip_factor("HARD");
        assert!(soft < medium, "SOFT ({soft}) should be faster than MEDIUM ({medium})");
        assert!(medium < hard, "MEDIUM ({medium}) should be faster than HARD ({hard})");
    }

    #[test]
    fn test_perf_factors_baseline() {
        let car = CarParams::default();
        let env = EnvironmentParams::default();
        let factors = compute_perf_factors(&car, &env, &env);
        // Baseline should give ~1.0 factor on straights and corners
        assert!((factors.straight_speed_factor - 1.0).abs() < 0.1);
        assert!((factors.corner_speed_factor - 1.0).abs() < 0.1);
    }

    #[test]
    fn test_perf_factors_more_power_faster() {
        let car_more_power = CarParams { engine_power_factor: 1.1, ..Default::default() };
        let car_baseline = CarParams::default();
        let env = EnvironmentParams::default();
        let f_more = compute_perf_factors(&car_more_power, &env, &env);
        let f_base = compute_perf_factors(&car_baseline, &env, &env);
        assert!(f_more.straight_speed_factor > f_base.straight_speed_factor,
            "More power should give higher straight speed factor");
        assert!(f_more.overall_lap_factor < f_base.overall_lap_factor,
            "More power should reduce lap time factor (faster)");
    }

    #[test]
    fn test_perf_factors_more_downforce() {
        let car = CarParams { aero_downforce_factor: 1.2, ..Default::default() };
        let env = EnvironmentParams::default();
        let f = compute_perf_factors(&car, &env, &env);
        let f_base = compute_perf_factors(&CarParams::default(), &env, &env);
        assert!(f.corner_speed_factor > f_base.corner_speed_factor,
            "More downforce should give higher corner speed factor");
    }

    #[test]
    fn test_tyre_degradation_soft_faster_than_hard() {
        let soft_deg = tyre_degradation_per_lap("SOFT", 1.0);
        let hard_deg = tyre_degradation_per_lap("HARD", 1.0);
        assert!(soft_deg > hard_deg, "SOFT tyres should degrade faster than HARD");
    }

    #[test]
    fn test_tyre_degradation_wear_rate_multiplier() {
        let normal = tyre_degradation_per_lap("MEDIUM", 1.0);
        let high = tyre_degradation_per_lap("MEDIUM", 2.0);
        assert!((high - 2.0 * normal).abs() < 1e-10, "Wear rate multiplier should scale linearly");
    }

    #[test]
    fn test_wind_increases_drag() {
        let env_no_wind = EnvironmentParams { wind_speed_ms: 0.0, ..Default::default() };
        let env_headwind = EnvironmentParams { wind_speed_ms: 10.0, wind_direction_deg: 0.0, ..Default::default() };
        let f_no = compute_perf_factors(&CarParams::default(), &env_no_wind, &env_no_wind);
        let f_wind = compute_perf_factors(&CarParams::default(), &env_headwind, &env_headwind);
        assert!(f_wind.overall_lap_factor > f_no.overall_lap_factor,
            "Headwind should increase lap time factor (slower)");
    }

    #[test]
    fn test_fuel_load_penalty() {
        let env = EnvironmentParams::default();
        let car_heavy = CarParams { fuel_load_kg: 105.0, ..Default::default() };
        let car_light = CarParams { fuel_load_kg: 80.0, ..Default::default() };
        let f_heavy = compute_perf_factors(&car_heavy, &env, &env);
        let f_light = compute_perf_factors(&car_light, &env, &env);
        assert!(f_heavy.overall_lap_factor > f_light.overall_lap_factor,
            "Heavier fuel load should be slower");
    }
}
