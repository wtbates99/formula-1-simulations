import type { CarConfig, SimConfig, Telemetry, TrackConfig } from './types';

type WasmModule = {
  HEAPF32: Float32Array;
  HEAPU32: Uint32Array;
  _malloc(size: number): number;
  _free(ptr: number): void;
  _init_sim(cfgPtr: number): number;
  _reset_sim(): void;
  _set_controls(throttle: number, brake: number, steering: number): void;
  _step_sim(dt: number): void;
  _get_vehicle_state(carIndex: number, outPtr: number): void;
  _run_lap(): number;
  _state_x_ptr(): number;
  _state_y_ptr(): number;
  _state_yaw_ptr(): number;
  _state_speed_ptr(): number;
  _state_car_count(): number;
};

export type Controls = {
  throttle: number;
  brake: number;
  steer: number;
};

const SIZE_F32 = 4;
const SIZE_U32 = 4;
const TRACK_NODE_SIZE = 12;
const TORQUE_POINT_SIZE = 8;
const INIT_CONFIG_SIZE = 136;
const VEHICLE_STATE_SIZE = 48;

const OFF_FIXED_DT = 0;
const OFF_MAX_CARS = 4;
const OFF_REPLAY_CAP = 8;
const OFF_ACTIVE_CARS = 12;
const OFF_TRACK_LEN = 16;
const OFF_TRACK_NODES_PTR = 20;
const OFF_TRACK_NODE_COUNT = 24;
const OFF_MASS = 28;
const OFF_WHEELBASE = 32;
const OFF_CG_FRONT = 36;
const OFF_CG_REAR = 40;
const OFF_TIRE_R = 44;
const OFF_MU_LONG = 48;
const OFF_MU_LAT = 52;
const OFF_CDA = 56;
const OFF_CLA = 60;
const OFF_ROLLING = 64;
const OFF_BRAKE = 68;
const OFF_STEER_GAIN = 72;
const OFF_GEAR_RATIOS = 76;
const OFF_GEAR_COUNT = 108;
const OFF_FINAL_DRIVE = 112;
const OFF_DRIVELINE = 116;
const OFF_SHIFT_UP = 120;
const OFF_SHIFT_DOWN = 124;
const OFF_TORQUE_PTR = 128;
const OFF_TORQUE_COUNT = 132;

const OFF_SNAP_SPEED = 16;
const OFF_SNAP_A_LONG = 20;
const OFF_SNAP_A_LAT = 24;
const OFF_SNAP_RPM = 28;
const OFF_SNAP_GEAR = 32;
const OFF_SNAP_LAP = 36;
const OFF_SNAP_LAP_TIME = 40;
const OFF_SNAP_LAST_LAP = 44;

export class SimClient {
  private mod!: WasmModule;
  private moduleReady = false;
  private dv!: DataView;
  private snapshotPtr = 0;

  private xView!: Float32Array;
  private yView!: Float32Array;
  private yawView!: Float32Array;
  private speedView!: Float32Array;

  private controls: Controls = { throttle: 0.9, brake: 0, steer: 0 };

  async init(simCfg: SimConfig, carCfg: CarConfig, trackCfg: TrackConfig): Promise<void> {
    if (!this.moduleReady) {
      const wasmJsUrl = new URL('/wasm/f1sim.js', window.location.origin).toString();
      const factory = (await import(/* @vite-ignore */ wasmJsUrl)).default as (opts: object) => Promise<WasmModule>;
      this.mod = await factory({});
      this.dv = new DataView(this.mod.HEAPF32.buffer);
      this.moduleReady = true;
    }

    if (this.snapshotPtr !== 0) {
      this.mod._free(this.snapshotPtr);
      this.snapshotPtr = 0;
    }

    const trackNodesPtr = this.mod._malloc(trackCfg.nodes.length * TRACK_NODE_SIZE);
    const torquePtr = this.mod._malloc(carCfg.powertrain.torque_curve.length * TORQUE_POINT_SIZE);
    const initCfgPtr = this.mod._malloc(INIT_CONFIG_SIZE);

    for (let i = 0; i < trackCfg.nodes.length; i += 1) {
      const [s, k, z] = trackCfg.nodes[i];
      const b = trackNodesPtr + i * TRACK_NODE_SIZE;
      this.dv.setFloat32(b + 0, s, true);
      this.dv.setFloat32(b + 4, k, true);
      this.dv.setFloat32(b + 8, z, true);
    }

    for (let i = 0; i < carCfg.powertrain.torque_curve.length; i += 1) {
      const [rpm, tq] = carCfg.powertrain.torque_curve[i];
      const b = torquePtr + i * TORQUE_POINT_SIZE;
      this.dv.setFloat32(b + 0, rpm, true);
      this.dv.setFloat32(b + 4, tq, true);
    }

    this.dv.setFloat32(initCfgPtr + OFF_FIXED_DT, simCfg.fixed_dt, true);
    this.dv.setUint32(initCfgPtr + OFF_MAX_CARS, simCfg.max_cars, true);
    this.dv.setUint32(initCfgPtr + OFF_REPLAY_CAP, simCfg.replay_capacity_steps, true);
    this.dv.setUint32(initCfgPtr + OFF_ACTIVE_CARS, simCfg.active_cars, true);

    this.dv.setFloat32(initCfgPtr + OFF_TRACK_LEN, trackCfg.length_m, true);
    this.dv.setUint32(initCfgPtr + OFF_TRACK_NODES_PTR, trackNodesPtr, true);
    this.dv.setUint32(initCfgPtr + OFF_TRACK_NODE_COUNT, trackCfg.nodes.length, true);

    this.dv.setFloat32(initCfgPtr + OFF_MASS, carCfg.mass_kg, true);
    this.dv.setFloat32(initCfgPtr + OFF_WHEELBASE, carCfg.wheelbase_m, true);
    this.dv.setFloat32(initCfgPtr + OFF_CG_FRONT, carCfg.cg_to_front_m, true);
    this.dv.setFloat32(initCfgPtr + OFF_CG_REAR, carCfg.cg_to_rear_m, true);
    this.dv.setFloat32(initCfgPtr + OFF_TIRE_R, carCfg.tire_radius_m, true);
    this.dv.setFloat32(initCfgPtr + OFF_MU_LONG, carCfg.mu_long, true);
    this.dv.setFloat32(initCfgPtr + OFF_MU_LAT, carCfg.mu_lat, true);
    this.dv.setFloat32(initCfgPtr + OFF_CDA, carCfg.cdA, true);
    this.dv.setFloat32(initCfgPtr + OFF_CLA, carCfg.clA, true);
    this.dv.setFloat32(initCfgPtr + OFF_ROLLING, carCfg.rolling_resistance, true);
    this.dv.setFloat32(initCfgPtr + OFF_BRAKE, carCfg.brake_force_max_n, true);
    this.dv.setFloat32(initCfgPtr + OFF_STEER_GAIN, carCfg.steer_gain, true);

    for (let i = 0; i < 8; i += 1) {
      this.dv.setFloat32(initCfgPtr + OFF_GEAR_RATIOS + i * SIZE_F32, carCfg.powertrain.gear_ratios[i] ?? 0, true);
    }

    this.dv.setUint32(initCfgPtr + OFF_GEAR_COUNT, carCfg.powertrain.gear_count, true);
    this.dv.setFloat32(initCfgPtr + OFF_FINAL_DRIVE, carCfg.powertrain.final_drive, true);
    this.dv.setFloat32(initCfgPtr + OFF_DRIVELINE, carCfg.powertrain.driveline_efficiency, true);
    this.dv.setFloat32(initCfgPtr + OFF_SHIFT_UP, carCfg.powertrain.shift_rpm_up, true);
    this.dv.setFloat32(initCfgPtr + OFF_SHIFT_DOWN, carCfg.powertrain.shift_rpm_down, true);
    this.dv.setUint32(initCfgPtr + OFF_TORQUE_PTR, torquePtr, true);
    this.dv.setUint32(initCfgPtr + OFF_TORQUE_COUNT, carCfg.powertrain.torque_curve.length, true);

    const ok = this.mod._init_sim(initCfgPtr);

    this.mod._free(trackNodesPtr);
    this.mod._free(torquePtr);
    this.mod._free(initCfgPtr);

    if (ok !== 1) throw new Error('init_sim failed');

    const cars = this.mod._state_car_count();
    this.snapshotPtr = this.mod._malloc(VEHICLE_STATE_SIZE);
    this.xView = new Float32Array(this.mod.HEAPF32.buffer, this.mod._state_x_ptr(), cars);
    this.yView = new Float32Array(this.mod.HEAPF32.buffer, this.mod._state_y_ptr(), cars);
    this.yawView = new Float32Array(this.mod.HEAPF32.buffer, this.mod._state_yaw_ptr(), cars);
    this.speedView = new Float32Array(this.mod.HEAPF32.buffer, this.mod._state_speed_ptr(), cars);
  }

  reset(): void {
    this.mod._reset_sim();
  }

  setControls(c: Controls): void {
    this.controls = c;
    this.mod._set_controls(c.throttle, c.brake, c.steer);
  }

  step(dt: number): void {
    this.mod._step_sim(dt);
  }

  runLap(): number {
    return this.mod._run_lap();
  }

  snapshotTelemetry(): Telemetry {
    this.mod._get_vehicle_state(0, this.snapshotPtr);

    const speed = this.dv.getFloat32(this.snapshotPtr + OFF_SNAP_SPEED, true);
    const gLong = this.dv.getFloat32(this.snapshotPtr + OFF_SNAP_A_LONG, true) / 9.80665;
    const gLat = this.dv.getFloat32(this.snapshotPtr + OFF_SNAP_A_LAT, true) / 9.80665;
    const rpm = this.dv.getFloat32(this.snapshotPtr + OFF_SNAP_RPM, true);
    const gear = this.dv.getUint32(this.snapshotPtr + OFF_SNAP_GEAR, true);
    const lap = this.dv.getUint32(this.snapshotPtr + OFF_SNAP_LAP, true);
    const lapTime = this.dv.getFloat32(this.snapshotPtr + OFF_SNAP_LAP_TIME, true);
    const lastLap = this.dv.getFloat32(this.snapshotPtr + OFF_SNAP_LAST_LAP, true);

    return {
      speedMps: speed,
      throttle: this.controls.throttle,
      brake: this.controls.brake,
      steer: this.controls.steer,
      gLong,
      gLat,
      lap,
      lapTime,
      lastLapTime: lastLap,
      lapDelta: lastLap > 0 ? lapTime - lastLap : 0,
      rpm,
      gear
    };
  }

  carViews(): { x: Float32Array; y: Float32Array; yaw: Float32Array; speed: Float32Array } {
    return { x: this.xView, y: this.yView, yaw: this.yawView, speed: this.speedView };
  }
}
