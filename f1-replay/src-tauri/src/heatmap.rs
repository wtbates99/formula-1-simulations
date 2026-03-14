use crate::types::HeatCell;
use std::collections::HashMap;

const BIN_SIZE: f32 = 60.0;

/// Compute a speed heatmap from track position + speed data.
///
/// - Bins positions into BIN_SIZE-unit cells
/// - Averages speed per bin
/// - Normalises: speed_norm = (speed - p5) / (p95 - p5), clamped [0,1]
/// - Drops cells with fewer than 5 samples
/// - Sorts by speed_norm ascending (slowest first, so fast cells render on top)
pub fn compute_heatmap(positions: &[(f32, f32)], speeds: &[f32]) -> Vec<HeatCell> {
    assert_eq!(positions.len(), speeds.len());

    // Accumulate speed sums and counts per bin
    let mut bins: HashMap<(i32, i32), (f64, u32)> = HashMap::new();

    for (&(px, py), &spd) in positions.iter().zip(speeds.iter()) {
        let bx = (px / BIN_SIZE).floor() as i32;
        let by = (py / BIN_SIZE).floor() as i32;
        let entry = bins.entry((bx, by)).or_insert((0.0, 0));
        entry.0 += spd as f64;
        entry.1 += 1;
    }

    // Filter cells with >= 5 samples and compute average speed
    let mut cells: Vec<(f32, f32, f32)> = bins
        .into_iter()
        .filter(|(_, (_, count))| *count >= 5)
        .map(|((bx, by), (sum, count))| {
            let cx = (bx as f32 + 0.5) * BIN_SIZE;
            let cy = (by as f32 + 0.5) * BIN_SIZE;
            let avg_speed = (sum / count as f64) as f32;
            (cx, cy, avg_speed)
        })
        .collect();

    if cells.is_empty() {
        return vec![];
    }

    // Collect speeds for percentile computation
    let mut sorted_speeds: Vec<f32> = cells.iter().map(|&(_, _, s)| s).collect();
    sorted_speeds.sort_by(|a, b| a.partial_cmp(b).unwrap());

    let p5 = percentile(&sorted_speeds, 5.0);
    let p95 = percentile(&sorted_speeds, 95.0);
    let range = p95 - p5;

    // Normalise speeds
    let mut result: Vec<HeatCell> = cells
        .into_iter()
        .map(|(cx, cy, spd)| {
            let norm = if range > 0.1 {
                ((spd - p5) / range).clamp(0.0, 1.0)
            } else {
                0.5
            };
            HeatCell { x: cx, y: cy, speed_norm: norm }
        })
        .collect();

    // Sort ascending by speed_norm (slowest first = rendered first = underneath)
    result.sort_by(|a, b| a.speed_norm.partial_cmp(&b.speed_norm).unwrap());

    result
}

fn percentile(sorted: &[f32], p: f64) -> f32 {
    if sorted.is_empty() {
        return 0.0;
    }
    let idx = (p / 100.0 * (sorted.len() - 1) as f64).round() as usize;
    sorted[idx.min(sorted.len() - 1)]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_heatmap_empty() {
        let cells = compute_heatmap(&[], &[]);
        assert!(cells.is_empty());
    }

    #[test]
    fn test_heatmap_normalization() {
        // All speeds in one bin → norm = 0.5 (single bin with p5=p95=same value)
        let positions: Vec<(f32, f32)> = (0..20).map(|i| (i as f32, 0.0)).collect();
        let speeds: Vec<f32> = (0..20).map(|i| 100.0 + i as f32 * 10.0).collect();
        let cells = compute_heatmap(&positions, &speeds);
        // All cells should have normalized values in [0, 1]
        for c in &cells {
            assert!(c.speed_norm >= 0.0, "speed_norm {} < 0", c.speed_norm);
            assert!(c.speed_norm <= 1.0, "speed_norm {} > 1", c.speed_norm);
        }
    }

    #[test]
    fn test_heatmap_min_samples_filter() {
        // Positions with only 2 samples per bin should be filtered (< 5)
        let positions: Vec<(f32, f32)> = (0..4).map(|i| (i as f32 * 1000.0, 0.0)).collect();
        let speeds = vec![100.0f32; 4];
        let cells = compute_heatmap(&positions, &speeds);
        assert!(cells.is_empty(), "Cells with < 5 samples should be filtered");
    }

    #[test]
    fn test_heatmap_two_speed_zones() {
        // Create two distinct speed zones
        let slow_positions: Vec<(f32, f32)> = (0..20).map(|_| (0.0, 0.0)).collect();
        let fast_positions: Vec<(f32, f32)> = (0..20).map(|_| (10000.0, 0.0)).collect();
        let slow_speeds = vec![50.0f32; 20];
        let fast_speeds = vec![300.0f32; 20];

        let mut positions = slow_positions;
        positions.extend(fast_positions);
        let mut speeds = slow_speeds;
        speeds.extend(fast_speeds);

        let cells = compute_heatmap(&positions, &speeds);
        assert_eq!(cells.len(), 2, "Should have exactly 2 speed zone cells");

        // Slow zone should have lower speed_norm
        let slow_cell = cells.iter().min_by(|a, b| a.speed_norm.partial_cmp(&b.speed_norm).unwrap());
        let fast_cell = cells.iter().max_by(|a, b| a.speed_norm.partial_cmp(&b.speed_norm).unwrap());
        assert!(slow_cell.unwrap().speed_norm < fast_cell.unwrap().speed_norm);
    }
}
