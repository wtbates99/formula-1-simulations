export type TrackConfig = {
  name: string;
  length_m: number;
  nodes: Array<[number, number, number]>;
};

export type CarConfig = {
  mass_kg: number;
  wheelbase_m: number;
  cg_to_front_m: number;
  cg_to_rear_m: number;
  tire_radius_m: number;
  mu_long: number;
  mu_lat: number;
  cdA: number;
  clA: number;
  rolling_resistance: number;
  brake_force_max_n: number;
  steer_gain: number;
  powertrain: {
    gear_ratios: number[];
    gear_count: number;
    final_drive: number;
    driveline_efficiency: number;
    shift_rpm_up: number;
    shift_rpm_down: number;
    torque_curve: Array<[number, number]>;
  };
};

export type SimConfig = {
  fixed_dt: number;
  max_cars: number;
  replay_capacity_steps: number;
  active_cars: number;
};

export type Telemetry = {
  speedMps: number;
  throttle: number;
  brake: number;
  steer: number;
  gLong: number;
  gLat: number;
  lap: number;
  lapTime: number;
  lastLapTime: number;
  lapDelta: number;
  rpm: number;
  gear: number;
};

export type BootstrapPayload = {
  sim: SimConfig;
  car: CarConfig;
  track: TrackConfig;
  meta: {
    year: number;
    round: number;
    session: string;
    driver: string;
    driver_number: string;
    event_name: string;
    points_used: number;
    selected_drivers?: Array<{
      driver: string;
      driver_number: string;
      team: string;
    }>;
    scenario?: {
      weather: string;
      tire: string;
      aggression: number;
      sector_tires: string[];
      sector_aggression: number[];
    };
  };
};

export type CatalogSessionsPayload = {
  sessions: Array<{
    year: number;
    round: number;
    session: string;
    event_name: string;
    driver_count: number;
    telemetry_rows: number;
  }>;
};

export type CatalogDriversPayload = {
  year: number;
  round: number;
  session: string;
  drivers: Array<{
    driver: string;
    driver_number: string;
    team: string;
    samples: number;
  }>;
};

export type ReplayPayload = {
  meta: {
    year: number;
    round: number;
    session: string;
    event_name: string;
    driver_count: number;
    frame_count: number;
    stride: number;
  };
  traces: Array<{
    driver: string;
    driver_number: string;
    team: string;
    x: number[];
    y: number[];
    speed: number[];
    rpm: number[];
    gear: number[];
    throttle: number[];
    brake: number[];
  }>;
};

export type SimBenchmarkPayload = {
  year: number;
  round: number;
  session: string;
  fastest_lap_s: number;
  fastest_driver: string;
  top_laps: Array<{
    driver: string;
    driver_number: string;
    lap_time_s: number;
  }>;
};
