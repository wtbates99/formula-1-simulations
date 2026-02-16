-- Top-level replay summary.
SELECT
  sim_id,
  COUNT(DISTINCT frame_idx) AS frames,
  COUNT(DISTINCT car_id) AS cars,
  ROUND(MAX(sim_time_s), 1) AS sim_seconds
FROM sim_replay_frames
GROUP BY sim_id
ORDER BY sim_seconds DESC;

-- Final order from last frame.
WITH latest AS (
  SELECT sim_id, MAX(frame_idx) AS frame_idx
  FROM sim_replay_frames
  GROUP BY sim_id
)
SELECT
  f.sim_id,
  f.position,
  f.car_id,
  f.lap,
  ROUND(f.distance_total_m, 1) AS distance_total_m,
  f.pit_stops
FROM sim_replay_frames f
JOIN latest l ON l.sim_id = f.sim_id AND l.frame_idx = f.frame_idx
ORDER BY f.sim_id, f.position;
