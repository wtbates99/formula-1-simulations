import './styles.css';

import { buildTrackPolyline, Renderer } from './renderer';
import { SimClient } from './sim-client';
import type {
  BootstrapPayload,
  CarConfig,
  CatalogDriversPayload,
  CatalogSessionsPayload,
  ReplayPayload,
  SimBenchmarkPayload,
  SimConfig,
  Telemetry,
  TrackConfig
} from './types';
import { createUI } from './ui';

type TelemetryHistory = {
  speed: Float32Array;
  gLat: Float32Array;
  cursor: number;
};

type ModeState = 'replay' | 'simulate';

function drawTelemetry(canvas: HTMLCanvasElement, hist: TelemetryHistory): void {
  const ctx = canvas.getContext('2d');
  if (!ctx) return;

  const w = canvas.width;
  const h = canvas.height;
  ctx.clearRect(0, 0, w, h);

  ctx.strokeStyle = '#24334d';
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i += 1) {
    const y = (h * i) / 4;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  }

  const len = hist.speed.length;

  ctx.strokeStyle = '#ff5c1b';
  ctx.lineWidth = 2;
  ctx.beginPath();
  for (let i = 0; i < len; i += 1) {
    const idx = (hist.cursor + i) % len;
    const x = (i / (len - 1)) * w;
    const y = h - Math.min(1, hist.speed[idx] / 110) * h;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.stroke();

  ctx.strokeStyle = '#2de0bd';
  ctx.lineWidth = 1.8;
  ctx.beginPath();
  for (let i = 0; i < len; i += 1) {
    const idx = (hist.cursor + i) % len;
    const x = (i / (len - 1)) * w;
    const y = h * 0.5 - Math.max(-1, Math.min(1, hist.gLat[idx] / 4.0)) * (h * 0.45);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.stroke();
}

async function loadJson<T>(url: string): Promise<T> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`Failed to load ${url}: ${res.status}`);
  return (await res.json()) as T;
}

function apiBases(): string[] {
  return [window.location.origin, 'http://127.0.0.1:8000', 'http://localhost:8000'];
}

async function apiGet<T>(path: string, params?: Record<string, string | number | undefined>): Promise<T> {
  const errs: string[] = [];
  for (const base of apiBases()) {
    const u = new URL(path, base);
    if (params) {
      for (const [k, v] of Object.entries(params)) {
        if (v === undefined) continue;
        u.searchParams.set(k, String(v));
      }
    }
    try {
      return await loadJson<T>(u.toString());
    } catch (e) {
      errs.push(`${u.origin}: ${String(e)}`);
    }
  }
  throw new Error(`API failed for ${path}: ${errs.join(' | ')}`);
}

async function boot(): Promise<void> {
  const app = document.querySelector<HTMLElement>('#app');
  if (!app) throw new Error('Missing #app');

  const ui = createUI(app, 'F1 DATA LAB');
  const renderer = new Renderer(ui.canvas);
  const sim = new SimClient();

  const history: TelemetryHistory = {
    speed: new Float32Array(320),
    gLat: new Float32Array(320),
    cursor: 0
  };

  let mode: ModeState = 'replay';
  let playing = true;
  let replay: ReplayPayload | null = null;
  let replayFrame = 0;
  let replayAccum = 0;
  let simCfg: SimConfig | null = null;
  let trackCfg: TrackConfig | null = null;
  let carCfg: CarConfig | null = null;
  let benchmark: SimBenchmarkPayload | null = null;

  const loadDriverList = async () => {
    const s = ui.selectedSession();
    const drivers = await apiGet<CatalogDriversPayload>('/api/catalog', {
      year: s.year,
      round: s.round,
      session: s.session
    });
    ui.setDriverOptions(
      drivers.drivers.map((d) => ({
        value: d.driver_number,
        label: `${d.driver} (${d.driver_number}) · ${d.team}`
      }))
    );
  };

  const loadSessions = async () => {
    const payload = await apiGet<CatalogSessionsPayload>('/api/catalog');
    const options = payload.sessions.map((s) => ({
      value: `${s.year}|${s.round}|${s.session}|${s.event_name}`,
      label: `${s.year} R${s.round} ${s.session} · ${s.event_name} · ${s.driver_count} drivers`
    }));
    ui.setSessionOptions(options);
    await loadDriverList();
  };

  const buildReplayTrack = (r: ReplayPayload): Float32Array => {
    const t = r.traces[0];
    const arr = new Float32Array(t.x.length * 2);
    for (let i = 0; i < t.x.length; i += 1) {
      arr[i * 2] = t.x[i];
      arr[i * 2 + 1] = t.y[i];
    }
    return arr;
  };

  const renderReplayFrame = (frame: number): Telemetry => {
    if (!replay) {
      return {
        speedMps: 0,
        throttle: 0,
        brake: 0,
        steer: 0,
        gLong: 0,
        gLat: 0,
        lap: 0,
        lapTime: 0,
        lastLapTime: 0,
        lapDelta: 0,
        rpm: 0,
        gear: 0
      };
    }

    const x = new Float32Array(replay.traces.length);
    const y = new Float32Array(replay.traces.length);
    const yaw = new Float32Array(replay.traces.length);
    const speed = new Float32Array(replay.traces.length);

    for (let i = 0; i < replay.traces.length; i += 1) {
      const tr = replay.traces[i];
      const idx = Math.min(frame, tr.x.length - 1);
      x[i] = tr.x[idx];
      y[i] = tr.y[idx];
      speed[i] = tr.speed[idx];
      if (idx > 0) {
        const dx = tr.x[idx] - tr.x[idx - 1];
        const dy = tr.y[idx] - tr.y[idx - 1];
        yaw[i] = Math.atan2(dy, dx);
      }
    }

    renderer.render({ x, y, yaw, speed });

    const lead = replay.traces[0];
    const i = Math.min(frame, lead.x.length - 1);
    return {
      speedMps: lead.speed[i],
      throttle: lead.throttle[i],
      brake: lead.brake[i],
      steer: 0,
      gLong: 0,
      gLat: 0,
      lap: 0,
      lapTime: i / 20,
      lastLapTime: 0,
      lapDelta: 0,
      rpm: lead.rpm[i],
      gear: lead.gear[i]
    };
  };

  const loadReplay = async () => {
    const s = ui.selectedSession();
    const drivers = ui.selectedDrivers();
    const sc = ui.readScenario();
    mode = 'replay';
    ui.setGuide('Replay mode: watching actual telemetry traces from SQLite.');
    ui.setStatusLine('Loading replay from SQLite telemetry...');

    replay = await apiGet<ReplayPayload>('/api/replay', {
      year: s.year,
      round: s.round,
      session: s.session,
      drivers: drivers.join(','),
      max_drivers: sc.maxDrivers,
      stride: 8
    });

    replayFrame = 0;
    replayAccum = 0;
    renderer.setTrack(buildReplayTrack(replay));
    ui.setTitle(`REPLAY · ${replay.meta.event_name} · ${replay.meta.session}`);
    ui.setCompare(`Drivers loaded: ${replay.meta.driver_count} · Frames: ${replay.meta.frame_count}`);
    ui.setStatusLine('Replay loaded. Press Play to watch all selected drivers.');
  };

  const loadSimulation = async () => {
    const s = ui.selectedSession();
    const drivers = ui.selectedDrivers();
    const sc = ui.readScenario();
    mode = 'simulate';
    ui.setGuide('Simulation mode: your configured driver setup in C++/Wasm, compared against real lap benchmarks.');
    ui.setStatusLine('Loading simulation bootstrap and benchmarks...');

    const bootstrap = await apiGet<BootstrapPayload>('/api/bootstrap-config', {
      year: s.year,
      round: s.round,
      session: s.session,
      drivers: drivers.join(','),
      max_drivers: sc.maxDrivers,
      weather: sc.weather,
      tire: sc.tire,
      aggression: sc.aggression
    });

    benchmark = await apiGet<SimBenchmarkPayload>('/api/benchmark', {
      year: s.year,
      round: s.round,
      session: s.session,
      drivers: drivers.join(',')
    });

    simCfg = bootstrap.sim;
    trackCfg = bootstrap.track;
    carCfg = bootstrap.car;

    renderer.setTrack(buildTrackPolyline(trackCfg, 2400));
    await sim.init(simCfg, carCfg, trackCfg);

    ui.setTitle(`SIMULATE · ${bootstrap.meta.event_name} · ${bootstrap.meta.session}`);
    ui.setCompare(
      benchmark.fastest_lap_s > 0
        ? `Actual fastest: ${benchmark.fastest_lap_s.toFixed(3)} s by ${benchmark.fastest_driver}`
        : 'No benchmark lap available in DB for selected session.'
    );
    ui.setStatusLine('Simulation loaded. Tune controls and press Run Lap to compare.');
  };

  ui.onSessionChange(() => {
    void loadDriverList().catch((err) => ui.setStatusLine(`Failed loading drivers: ${String(err)}`));
  });

  ui.onLoadData(() => {
    const sc = ui.readScenario();
    void (sc.mode === 'replay' ? loadReplay() : loadSimulation()).catch((err) => {
      ui.setStatusLine(`Load failed: ${String(err)}`);
    });
  });

  ui.onPlayPause(() => {
    playing = !playing;
    ui.setPlaying(playing);
  });

  ui.onStep(() => {
    if (mode === 'replay') {
      replayFrame += 1;
    } else if (simCfg) {
      sim.step(simCfg.fixed_dt);
    }
  });

  ui.onReset(() => {
    if (mode === 'replay') {
      replayFrame = 0;
      replayAccum = 0;
    } else {
      sim.reset();
    }
  });

  ui.onRunLap(() => {
    if (mode !== 'simulate') {
      ui.setCompare('Run Lap is for simulation mode. Switch mode to Simulate My Driver vs Actual.');
      return;
    }
    const lap = sim.runLap();
    if (!benchmark || benchmark.fastest_lap_s <= 0) {
      ui.setCompare(`Simulated lap: ${lap.toFixed(3)} s (no actual benchmark in DB)`);
      return;
    }
    const delta = lap - benchmark.fastest_lap_s;
    ui.setCompare(
      `Simulated ${lap.toFixed(3)} s vs actual ${benchmark.fastest_lap_s.toFixed(3)} s (${delta >= 0 ? '+' : ''}${delta.toFixed(3)} s)`
    );
  });

  await loadSessions();
  await loadReplay();
  ui.setPlaying(playing);

  let lastTime = performance.now();
  const frame = (now: number) => {
    const dt = Math.min(0.1, (now - lastTime) / 1000);
    lastTime = now;

    const s = ui.readState();

    let t: Telemetry;
    if (mode === 'replay' && replay) {
      if (playing) {
        replayAccum += dt * s.speedMultiplier * 20.0;
        const whole = Math.floor(replayAccum);
        if (whole > 0) {
          replayFrame = (replayFrame + whole) % Math.max(1, replay.meta.frame_count);
          replayAccum -= whole;
        }
      }
      t = renderReplayFrame(replayFrame);
    } else {
      if (simCfg && carCfg && trackCfg) {
        carCfg = {
          ...carCfg,
          mass_kg: s.massKg,
          clA: s.downforce,
          mu_lat: s.grip
        };
        sim.setControls({ throttle: s.throttle, brake: s.brake, steer: s.steer });
        if (playing) sim.step(dt * s.speedMultiplier);
      }
      t = sim.snapshotTelemetry();
      renderer.render(sim.carViews());
    }

    ui.setTelemetry(t);
    history.speed[history.cursor] = t.speedMps;
    history.gLat[history.cursor] = t.gLat;
    history.cursor = (history.cursor + 1) % history.speed.length;
    drawTelemetry(ui.telemetryCanvas, history);

    requestAnimationFrame(frame);
  };

  requestAnimationFrame(frame);
}

void boot().catch((err: unknown) => {
  console.error(err);
  const app = document.querySelector<HTMLElement>('#app');
  if (app) {
    app.innerHTML = `
      <div style="padding:16px;color:#fff;font-family:Rajdhani,sans-serif;background:#141a24;">
        <h2>Simulation failed to start</h2>
        <p>Ensure the SQLite API is running:</p>
        <pre>./scripts/run_data_api.sh</pre>
        <pre style="white-space:pre-wrap;">${String(err)}</pre>
      </div>
    `;
  }
});
