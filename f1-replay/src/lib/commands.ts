import { invoke } from '@tauri-apps/api/core';

// ── Types returned FROM Rust (Rust serializes with snake_case by default) ──────
export interface SessionInfo   { event_name: string; session: string; year: number | null; }
export interface DriverFrame   {
  driver_number: string; x: number; y: number; heading: number;
  speed: number; gear: number; throttle: number; brake: number;
  drs_active: boolean; position: number; compound: string;
  tyre_life: number; is_in_pit: boolean;
}
export interface FrameData     { time_s: number; drivers: DriverFrame[]; }
export interface HeatCell      { x: number; y: number; speed_norm: number; }
export interface TrackLayout   {
  center_line: [number, number][];
  x_min: number; x_max: number; y_min: number; y_max: number;
  duration_s: number; lap_distance_m: number;
}
export interface DriverTelemetry {
  driver_number: string; times: number[]; speeds: number[];
  gears: number[]; throttles: number[]; brakes: number[];
}
export interface DriverMeta    { driver_number: string; abbreviation: string; team: string; }

// ── Distance-normalised comparison types ──────────────────────────────────────
export interface MiniSector {
  distance_start: number;
  distance_end: number;
  delta_s: number;  // positive = A faster, negative = B faster
}

export interface LapComparison {
  driver_a: string;
  driver_b: string;
  lap_number_a: number;
  lap_number_b: number;
  lap_time_a: number;
  lap_time_b: number;
  lap_time_delta: number;
  distances: number[];
  speeds_a: number[];
  speeds_b: number[];
  throttles_a: number[];
  throttles_b: number[];
  brakes_a: number[];
  brakes_b: number[];
  gears_a: number[];
  gears_b: number[];
  delta_time: number[];  // positive = A ahead
  mini_sectors: MiniSector[];
}

export interface AeroFitResult {
  driver_number: string;
  cda: number;
  c_roll: number;
  r_squared: number;
  sample_count: number;
}

export interface LapDelta {
  lap_number: number; time_a: number; time_b: number;
  delta_s: number; cumulative_delta_s: number;
}

export interface DriverComparison {
  driver_a: string; driver_b: string;
  fastest_lap_a: number; fastest_lap_b: number;
  avg_lap_a: number; avg_lap_b: number;
  max_speed_a: number; max_speed_b: number;
  lap_deltas: LapDelta[];
  summary: string;
}
export interface RaceAnalysis {
  event_name: string;
  session: string;
  driver_count: number;
  valid_lap_count: number;
  fastest_driver: string | null;
  fastest_lap_s: number | null;
  median_race_pace_s: number | null;
  drivers: DriverAnalysis[];
  stints: StintAnalysis[];
  insights: RaceInsight[];
}
export interface DriverAnalysis {
  driver_number: string;
  abbreviation: string;
  team: string;
  final_position: number;
  positions_gained: number;
  valid_laps: number;
  fastest_lap_s: number | null;
  median_lap_s: number | null;
  consistency_s: number | null;
  max_speed_kmh: number;
  avg_speed_kmh: number;
  avg_throttle_pct: number;
  avg_brake_pct: number;
  drs_usage_pct: number;
  pit_laps: number[];
  performance_score: number;
}
export interface StintAnalysis {
  driver_number: string;
  start_lap: number;
  end_lap: number;
  compound: string;
  laps: number;
  avg_lap_s: number | null;
  tyre_life_start: number;
  tyre_life_end: number;
}
export interface RaceInsight {
  kind: string;
  title: string;
  detail: string;
  driver_number: string | null;
  severity: number;
}

// ── Tauri v2: snake_case Rust param names → camelCase in invoke() args ─────────

export const getSessions       = () => invoke<SessionInfo[]>('get_sessions');

export const loadSessionCmd    = (eventName: string, session: string) =>
  invoke<TrackLayout>('load_session_cmd', { eventName, session });

export const getSpeedHeatmap   = () => invoke<HeatCell[]>('get_speed_heatmap');

export const getFrame          = (timeS: number) =>
  invoke<FrameData>('get_frame', { timeS });

export const getDriverTelemetry = (driverNumber: string, timeStart: number, timeEnd: number) =>
  invoke<DriverTelemetry>('get_driver_telemetry', { driverNumber, timeStart, timeEnd });

export const getDriverMeta     = () => invoke<DriverMeta[]>('get_driver_meta');

export const compareLapsCmd    = (driverA: string, driverB: string) =>
  invoke<LapComparison>('compare_laps_cmd', { driverA, driverB });

export const getAeroFitCmd     = (driverNumber: string) =>
  invoke<AeroFitResult>('get_aero_fit_cmd', { driverNumber });

export const getRaceAnalysis   = () =>
  invoke<RaceAnalysis>('get_race_analysis');
