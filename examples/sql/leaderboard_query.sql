-- Example: compute average lap pace for one race.
SELECT
  driver_id,
  COUNT(*) AS lap_count,
  ROUND(AVG(lap_time_ms), 1) AS avg_lap_ms,
  MIN(lap_time_ms) AS best_lap_ms
FROM telemetry_lap_timings
WHERE season = 2024 AND round = 1
GROUP BY driver_id
ORDER BY avg_lap_ms ASC;
