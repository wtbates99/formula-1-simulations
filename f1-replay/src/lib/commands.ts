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
  x_min: number; x_max: number; y_min: number; y_max: number; duration_s: number;
}
export interface DriverTelemetry {
  driver_number: string; times: number[]; speeds: number[];
  gears: number[]; throttles: number[]; brakes: number[];
}
export interface DriverMeta    { driver_number: string; abbreviation: string; team: string; }

// ── Tauri v2: snake_case Rust param names → camelCase in invoke() args ─────────

export const getSessions       = () => invoke<SessionInfo[]>('get_sessions');

// event_name → eventName,  session stays session
export const loadSessionCmd    = (eventName: string, session: string) =>
  invoke<TrackLayout>('load_session_cmd', { eventName, session });

export const getSpeedHeatmap   = () => invoke<HeatCell[]>('get_speed_heatmap');

// time_s → timeS
export const getFrame          = (timeS: number) =>
  invoke<FrameData>('get_frame', { timeS });

// driver_number → driverNumber, time_start → timeStart, time_end → timeEnd
export const getDriverTelemetry = (driverNumber: string, timeStart: number, timeEnd: number) =>
  invoke<DriverTelemetry>('get_driver_telemetry', { driverNumber, timeStart, timeEnd });

export const getDriverMeta     = () => invoke<DriverMeta[]>('get_driver_meta');
