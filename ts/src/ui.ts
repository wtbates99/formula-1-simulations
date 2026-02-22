import type { Telemetry } from './types';

export type UIState = {
  playing: boolean;
  speedMultiplier: number;
  throttle: number;
  brake: number;
  steer: number;
  massKg: number;
  downforce: number;
  grip: number;
};

export type ScenarioState = {
  mode: 'replay' | 'simulate';
  weather: 'dry' | 'damp' | 'wet';
  tire: 'hard' | 'medium' | 'soft';
  aggression: number;
  maxDrivers: number;
};

export type SessionChoice = {
  year: number;
  round: number;
  session: string;
  eventName: string;
};

export type OptionItem = {
  value: string;
  label: string;
};

export type UI = {
  canvas: HTMLCanvasElement;
  telemetryCanvas: HTMLCanvasElement;
  readState: () => UIState;
  readScenario: () => ScenarioState;
  selectedSession: () => SessionChoice;
  selectedDrivers: () => string[];
  setTitle: (title: string) => void;
  setGuide: (msg: string) => void;
  setCompare: (msg: string) => void;
  setStatusLine: (msg: string) => void;
  setPlaying: (playing: boolean) => void;
  setTelemetry: (t: Telemetry) => void;
  setSessionOptions: (options: OptionItem[]) => void;
  setDriverOptions: (options: OptionItem[]) => void;
  onSessionChange: (cb: () => void) => void;
  onLoadData: (cb: () => void) => void;
  onPlayPause: (cb: () => void) => void;
  onStep: (cb: () => void) => void;
  onReset: (cb: () => void) => void;
  onRunLap: (cb: () => void) => void;
};

export function createUI(root: HTMLElement, title: string): UI {
  root.innerHTML = `
    <div class="shell">
      <header class="topbar">
        <h1 id="sim-title">${title}</h1>
        <div class="status" id="sim-status">PAUSED</div>
      </header>
      <main class="layout">
        <section class="viewport-panel">
          <canvas id="track-canvas"></canvas>
        </section>
        <aside class="hud-panel">
          <div class="guide" id="guide-msg">
            1) Pick race/session and drivers. 2) Choose Replay or Simulate. 3) Load Data.
          </div>

          <div class="controls setup-grid">
            <label>Mode
              <select id="s-mode">
                <option value="replay">Replay Actual Race</option>
                <option value="simulate">Simulate My Driver vs Actual</option>
              </select>
            </label>

            <label>Race Session
              <select id="s-session"></select>
            </label>

            <label>Drivers (multi-select)
              <select id="s-drivers" multiple size="6"></select>
            </label>

            <label>Weather
              <select id="s-weather">
                <option value="dry">Dry</option>
                <option value="damp">Damp</option>
                <option value="wet">Wet</option>
              </select>
            </label>

            <label>Tire Strategy
              <select id="s-tire">
                <option value="medium">Medium</option>
                <option value="soft">Soft</option>
                <option value="hard">Hard</option>
              </select>
            </label>

            <label>Aggression
              <input id="c-aggr" type="range" min="0.7" max="1.5" step="0.01" value="1.0" />
            </label>

            <label>Driver Count
              <input id="c-max-drivers" type="range" min="1" max="20" step="1" value="12" />
            </label>

            <button id="b-load">Load Data</button>
          </div>

          <div class="status-line" id="status-line">Ready.</div>
          <div class="compare" id="compare-line">Comparison: --</div>

          <div class="metrics">
            <div><span>Speed</span><strong id="m-speed">0.0 km/h</strong></div>
            <div><span>Throttle / Brake</span><strong id="m-input">0.00 / 0.00</strong></div>
            <div><span>Steer</span><strong id="m-steer">0.00</strong></div>
            <div><span>G Long / Lat</span><strong id="m-g">0.00 / 0.00</strong></div>
            <div><span>Lap</span><strong id="m-lap">0</strong></div>
            <div><span>Lap Time</span><strong id="m-lap-time">0.000 s</strong></div>
            <div><span>Lap Delta</span><strong id="m-delta">+0.000 s</strong></div>
            <div><span>Powertrain</span><strong id="m-power">0 rpm · G1</strong></div>
          </div>

          <canvas id="telemetry-canvas" width="340" height="120"></canvas>

          <div class="controls">
            <label>Playback Speed <input id="c-speed" type="range" min="0.25" max="8" step="0.25" value="1" /></label>
            <label>Throttle <input id="c-throttle" type="range" min="0" max="1" step="0.01" value="0.9" /></label>
            <label>Brake <input id="c-brake" type="range" min="0" max="1" step="0.01" value="0" /></label>
            <label>Steer <input id="c-steer" type="range" min="-1" max="1" step="0.01" value="0" /></label>
            <label>Mass kg <input id="c-mass" type="range" min="720" max="900" step="1" value="798" /></label>
            <label>Downforce clA <input id="c-cla" type="range" min="2.0" max="4.8" step="0.05" value="3.2" /></label>
            <label>Grip mu_lat <input id="c-grip" type="range" min="1.4" max="2.6" step="0.01" value="2.1" /></label>
          </div>

          <div class="buttons">
            <button id="b-play">Play</button>
            <button id="b-step">Step</button>
            <button id="b-reset">Reset</button>
            <button id="b-runlap">Run Lap</button>
          </div>
        </aside>
      </main>
    </div>
  `;

  const q = <T extends HTMLElement>(sel: string): T => {
    const el = root.querySelector<T>(sel);
    if (!el) throw new Error(`missing ${sel}`);
    return el;
  };

  const status = q<HTMLElement>('#sim-status');
  const mSpeed = q<HTMLElement>('#m-speed');
  const mInput = q<HTMLElement>('#m-input');
  const mSteer = q<HTMLElement>('#m-steer');
  const mG = q<HTMLElement>('#m-g');
  const mLap = q<HTMLElement>('#m-lap');
  const mLapTime = q<HTMLElement>('#m-lap-time');
  const mDelta = q<HTMLElement>('#m-delta');
  const mPower = q<HTMLElement>('#m-power');

  const sessionSel = q<HTMLSelectElement>('#s-session');
  const driversSel = q<HTMLSelectElement>('#s-drivers');

  return {
    canvas: q<HTMLCanvasElement>('#track-canvas'),
    telemetryCanvas: q<HTMLCanvasElement>('#telemetry-canvas'),
    readState: () => ({
      playing: status.textContent === 'PLAYING',
      speedMultiplier: Number(q<HTMLInputElement>('#c-speed').value),
      throttle: Number(q<HTMLInputElement>('#c-throttle').value),
      brake: Number(q<HTMLInputElement>('#c-brake').value),
      steer: Number(q<HTMLInputElement>('#c-steer').value),
      massKg: Number(q<HTMLInputElement>('#c-mass').value),
      downforce: Number(q<HTMLInputElement>('#c-cla').value),
      grip: Number(q<HTMLInputElement>('#c-grip').value)
    }),
    readScenario: () => ({
      mode: q<HTMLSelectElement>('#s-mode').value as 'replay' | 'simulate',
      weather: q<HTMLSelectElement>('#s-weather').value as 'dry' | 'damp' | 'wet',
      tire: q<HTMLSelectElement>('#s-tire').value as 'hard' | 'medium' | 'soft',
      aggression: Number(q<HTMLInputElement>('#c-aggr').value),
      maxDrivers: Number(q<HTMLInputElement>('#c-max-drivers').value)
    }),
    selectedSession: () => {
      const raw = sessionSel.value;
      const [year, round, session, ...eventParts] = raw.split('|');
      return {
        year: Number(year),
        round: Number(round),
        session,
        eventName: eventParts.join('|')
      };
    },
    selectedDrivers: () => Array.from(driversSel.selectedOptions).map((o) => o.value),
    setTitle: (msg: string) => {
      q<HTMLElement>('#sim-title').textContent = msg;
    },
    setGuide: (msg: string) => {
      q<HTMLElement>('#guide-msg').textContent = msg;
    },
    setCompare: (msg: string) => {
      q<HTMLElement>('#compare-line').textContent = msg;
    },
    setStatusLine: (msg: string) => {
      q<HTMLElement>('#status-line').textContent = msg;
    },
    setSessionOptions: (options: OptionItem[]) => {
      sessionSel.innerHTML = options
        .map((o) => `<option value="${o.value.replace(/"/g, '&quot;')}">${o.label}</option>`)
        .join('');
    },
    setDriverOptions: (options: OptionItem[]) => {
      driversSel.innerHTML = options
        .map((o) => `<option value="${o.value.replace(/"/g, '&quot;')}">${o.label}</option>`)
        .join('');
      for (let i = 0; i < Math.min(8, driversSel.options.length); i += 1) {
        driversSel.options[i].selected = true;
      }
    },
    setPlaying: (playing: boolean) => {
      status.textContent = playing ? 'PLAYING' : 'PAUSED';
      q<HTMLButtonElement>('#b-play').textContent = playing ? 'Pause' : 'Play';
    },
    setTelemetry: (t: Telemetry) => {
      mSpeed.textContent = `${(t.speedMps * 3.6).toFixed(1)} km/h`;
      mInput.textContent = `${t.throttle.toFixed(2)} / ${t.brake.toFixed(2)}`;
      mSteer.textContent = t.steer.toFixed(2);
      mG.textContent = `${t.gLong.toFixed(2)} / ${t.gLat.toFixed(2)}`;
      mLap.textContent = `${t.lap}`;
      mLapTime.textContent = `${t.lapTime.toFixed(3)} s`;
      mDelta.textContent = `${t.lapDelta >= 0 ? '+' : ''}${t.lapDelta.toFixed(3)} s`;
      mPower.textContent = `${Math.round(t.rpm)} rpm · G${t.gear}`;
    },
    onSessionChange: (cb: () => void) => sessionSel.addEventListener('change', cb),
    onLoadData: (cb: () => void) => q<HTMLButtonElement>('#b-load').addEventListener('click', cb),
    onPlayPause: (cb: () => void) => q<HTMLButtonElement>('#b-play').addEventListener('click', cb),
    onStep: (cb: () => void) => q<HTMLButtonElement>('#b-step').addEventListener('click', cb),
    onReset: (cb: () => void) => q<HTMLButtonElement>('#b-reset').addEventListener('click', cb),
    onRunLap: (cb: () => void) => q<HTMLButtonElement>('#b-runlap').addEventListener('click', cb)
  };
}
