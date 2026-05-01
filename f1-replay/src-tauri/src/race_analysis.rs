use crate::session::{DriverData, LapRecord, SessionData};
use crate::types::{DriverAnalysis, RaceAnalysis, RaceInsight, StintAnalysis};

pub fn analyze_race(session: &SessionData) -> RaceAnalysis {
    let mut drivers: Vec<DriverAnalysis> = session.drivers.iter().map(analyze_driver).collect();
    let mut all_laps: Vec<f64> = drivers.iter().filter_map(|d| d.median_lap_s).collect();
    all_laps.sort_by(f64::total_cmp);

    let median_race_pace_s = median(&all_laps);
    let fastest = drivers
        .iter()
        .filter_map(|d| d.fastest_lap_s.map(|lap| (d.driver_number.clone(), lap)))
        .min_by(|a, b| a.1.total_cmp(&b.1));

    score_drivers(&mut drivers, median_race_pace_s);
    drivers.sort_by(|a, b| b.performance_score.total_cmp(&a.performance_score));

    let stints: Vec<StintAnalysis> = session.drivers.iter().flat_map(build_stints).collect();

    let insights = build_insights(&drivers, &stints, median_race_pace_s);

    RaceAnalysis {
        event_name: session.event_name.clone(),
        session: session.session.clone(),
        driver_count: session.drivers.len(),
        valid_lap_count: drivers.iter().map(|d| d.valid_laps).sum(),
        fastest_driver: fastest.as_ref().map(|f| f.0.clone()),
        fastest_lap_s: fastest.map(|f| f.1),
        median_race_pace_s,
        drivers,
        stints,
        insights,
    }
}

fn analyze_driver(driver: &DriverData) -> DriverAnalysis {
    let lap_times = valid_lap_times(driver);
    let fastest_lap_s = lap_times.iter().copied().min_by(f64::total_cmp);
    let median_lap_s = {
        let mut times = lap_times.clone();
        times.sort_by(f64::total_cmp);
        median(&times)
    };
    let consistency_s = lap_stddev(&lap_times);
    let first_position = driver.laps.first().map(|l| l.position).unwrap_or(20);
    let final_position = driver
        .laps
        .last()
        .map(|l| l.position)
        .unwrap_or(first_position);
    let pit_laps = pit_laps(driver);

    let moving_samples: Vec<_> = driver.samples.iter().filter(|s| s.speed > 20.0).collect();
    let sample_count = moving_samples.len().max(1) as f32;
    let max_speed_kmh = driver.samples.iter().map(|s| s.speed).fold(0.0, f32::max);
    let avg_speed_kmh = moving_samples.iter().map(|s| s.speed).sum::<f32>() / sample_count;
    let avg_throttle_pct =
        moving_samples.iter().map(|s| s.throttle).sum::<f32>() * 100.0 / sample_count;
    let avg_brake_pct = moving_samples.iter().map(|s| s.brake).sum::<f32>() * 100.0 / sample_count;
    let drs_usage_pct = moving_samples
        .iter()
        .filter(|s| matches!(s.drs, 10 | 12 | 14))
        .count() as f32
        * 100.0
        / sample_count;

    DriverAnalysis {
        driver_number: driver.driver_number.clone(),
        abbreviation: driver.abbreviation.clone(),
        team: driver.team.clone(),
        final_position,
        positions_gained: first_position as i16 - final_position as i16,
        valid_laps: lap_times.len(),
        fastest_lap_s,
        median_lap_s,
        consistency_s,
        max_speed_kmh,
        avg_speed_kmh,
        avg_throttle_pct,
        avg_brake_pct,
        drs_usage_pct,
        pit_laps,
        performance_score: 0.0,
    }
}

fn score_drivers(drivers: &mut [DriverAnalysis], field_median: Option<f64>) {
    let Some(field_median) = field_median else {
        return;
    };

    for driver in drivers {
        let pace_score = driver
            .median_lap_s
            .map(|pace| ((field_median - pace) * 8.0).clamp(-35.0, 35.0))
            .unwrap_or(-20.0);
        let consistency_score = driver
            .consistency_s
            .map(|stddev| (12.0 - stddev * 6.0).clamp(-12.0, 12.0))
            .unwrap_or(0.0);
        let position_score = driver.positions_gained as f64 * 2.5;
        let speed_score = (driver.max_speed_kmh as f64 - 330.0).clamp(-20.0, 20.0) * 0.25;

        driver.performance_score =
            50.0 + pace_score + consistency_score + position_score + speed_score;
    }
}

fn build_stints(driver: &DriverData) -> Vec<StintAnalysis> {
    let mut stints = Vec::new();
    let mut current: Vec<&LapRecord> = Vec::new();
    let mut current_compound = String::new();

    for lap in &driver.laps {
        if current.is_empty() || lap.compound == current_compound {
            if current.is_empty() {
                current_compound = lap.compound.clone();
            }
            current.push(lap);
            continue;
        }

        push_stint(driver, &current, &mut stints);
        current.clear();
        current_compound = lap.compound.clone();
        current.push(lap);
    }

    push_stint(driver, &current, &mut stints);
    stints
}

fn push_stint(driver: &DriverData, laps: &[&LapRecord], stints: &mut Vec<StintAnalysis>) {
    if laps.is_empty() {
        return;
    }

    let start_lap = laps.first().unwrap().lap_number;
    let end_lap = laps.last().unwrap().lap_number;
    let compound = laps.first().unwrap().compound.clone();
    let lap_times = lap_times_for_range(driver, start_lap, end_lap);
    let avg_lap_s = if lap_times.is_empty() {
        None
    } else {
        Some(lap_times.iter().sum::<f64>() / lap_times.len() as f64)
    };

    stints.push(StintAnalysis {
        driver_number: driver.driver_number.clone(),
        start_lap,
        end_lap,
        compound,
        laps: laps.len(),
        avg_lap_s,
        tyre_life_start: laps.first().unwrap().tyre_life,
        tyre_life_end: laps.last().unwrap().tyre_life,
    });
}

fn build_insights(
    drivers: &[DriverAnalysis],
    stints: &[StintAnalysis],
    field_median: Option<f64>,
) -> Vec<RaceInsight> {
    let mut insights = Vec::new();

    if let Some(best) = drivers
        .iter()
        .filter_map(|d| d.fastest_lap_s.map(|l| (d, l)))
        .min_by(|a, b| a.1.total_cmp(&b.1))
    {
        insights.push(RaceInsight {
            kind: "pace".to_string(),
            title: format!("{} set the fastest lap", best.0.abbreviation),
            detail: format!(
                "Fastest clean lap was {:.3}s, with a median race pace of {}.",
                best.1,
                format_optional_time(field_median)
            ),
            driver_number: Some(best.0.driver_number.clone()),
            severity: 0.95,
        });
    }

    if let Some(gainer) = drivers.iter().max_by_key(|d| d.positions_gained) {
        if gainer.positions_gained > 0 {
            insights.push(RaceInsight {
                kind: "racecraft".to_string(),
                title: format!(
                    "{} gained {} places",
                    gainer.abbreviation, gainer.positions_gained
                ),
                detail: format!(
                    "Started around P{} and finished P{} based on lap records.",
                    gainer.final_position as i16 + gainer.positions_gained,
                    gainer.final_position
                ),
                driver_number: Some(gainer.driver_number.clone()),
                severity: (0.55 + gainer.positions_gained as f32 * 0.06).min(0.95),
            });
        }
    }

    if let Some(consistent) = drivers
        .iter()
        .filter(|d| d.valid_laps >= 5)
        .filter_map(|d| d.consistency_s.map(|c| (d, c)))
        .min_by(|a, b| a.1.total_cmp(&b.1))
    {
        insights.push(RaceInsight {
            kind: "consistency".to_string(),
            title: format!("{} had the tightest lap spread", consistent.0.abbreviation),
            detail: format!(
                "Clean-lap standard deviation was {:.3}s across {} valid laps.",
                consistent.1, consistent.0.valid_laps
            ),
            driver_number: Some(consistent.0.driver_number.clone()),
            severity: 0.75,
        });
    }

    if let Some(long_stint) = stints.iter().max_by_key(|s| s.laps) {
        insights.push(RaceInsight {
            kind: "strategy".to_string(),
            title: format!(
                "Longest stint: {} laps on {}",
                long_stint.laps, long_stint.compound
            ),
            detail: format!(
                "Driver {} ran laps {}-{} with average lap {}.",
                long_stint.driver_number,
                long_stint.start_lap,
                long_stint.end_lap,
                format_optional_time(long_stint.avg_lap_s)
            ),
            driver_number: Some(long_stint.driver_number.clone()),
            severity: 0.6,
        });
    }

    insights.sort_by(|a, b| b.severity.total_cmp(&a.severity));
    insights
}

fn valid_lap_times(driver: &DriverData) -> Vec<f64> {
    let mut times = Vec::new();
    for i in 0..driver.laps.len().saturating_sub(1) {
        let lap = &driver.laps[i];
        let next = &driver.laps[i + 1];
        let dt = next.lap_start_time_s - lap.lap_start_time_s;
        if (60.0..200.0).contains(&dt) {
            times.push(dt);
        }
    }
    times
}

fn lap_times_for_range(driver: &DriverData, start_lap: u32, end_lap: u32) -> Vec<f64> {
    let mut times = Vec::new();
    for i in 0..driver.laps.len().saturating_sub(1) {
        let lap = &driver.laps[i];
        if lap.lap_number < start_lap || lap.lap_number > end_lap {
            continue;
        }
        let dt = driver.laps[i + 1].lap_start_time_s - lap.lap_start_time_s;
        if (60.0..200.0).contains(&dt) {
            times.push(dt);
        }
    }
    times
}

fn pit_laps(driver: &DriverData) -> Vec<u32> {
    driver
        .laps
        .windows(2)
        .filter_map(|pair| {
            let a = &pair[0];
            let b = &pair[1];
            if a.compound != b.compound || b.tyre_life < a.tyre_life {
                Some(b.lap_number)
            } else {
                None
            }
        })
        .collect()
}

fn lap_stddev(values: &[f64]) -> Option<f64> {
    if values.len() < 2 {
        return None;
    }
    let mean = values.iter().sum::<f64>() / values.len() as f64;
    let variance = values.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / values.len() as f64;
    Some(variance.sqrt())
}

fn median(values: &[f64]) -> Option<f64> {
    if values.is_empty() {
        return None;
    }
    let mid = values.len() / 2;
    if values.len() % 2 == 0 {
        Some((values[mid - 1] + values[mid]) / 2.0)
    } else {
        Some(values[mid])
    }
}

fn format_optional_time(value: Option<f64>) -> String {
    value
        .map(|v| format!("{v:.3}s"))
        .unwrap_or_else(|| "unavailable".to_string())
}
