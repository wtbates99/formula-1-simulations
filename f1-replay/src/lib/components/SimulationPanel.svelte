<script lang="ts">
  import { createEventDispatcher } from 'svelte';
  import { invoke } from '@tauri-apps/api/core';

  export let driverMeta: Array<{driver_number: string, abbreviation: string, team: string}> = [];
  export let focusedDriver: string | null = null;
  export let eventName: string = '';
  export let session: string = '';

  const dispatch = createEventDispatcher();

  // Scenario parameters
  let enginePower = 1.0;      // 0.8 - 1.2
  let downforce = 1.0;        // 0.7 - 1.3
  let drag = 1.0;             // 0.7 - 1.3
  let compound: string = 'HARD';
  let tyreWear = 1.0;
  let fuelLoad = 95;
  let windSpeed = 0;
  let windDir = 0;
  let trackTemp = 35;
  let swapCar: string = '';
  let swapDriver: string = '';

  let isRunning = false;
  let result: any = null;
  let error: string = '';

  const compounds = ['SOFT', 'MEDIUM', 'HARD', 'INTER', 'WET'];

  async function runSim() {
    if (!focusedDriver || !eventName) {
      error = 'Select a driver first';
      return;
    }
    isRunning = true;
    error = '';
    result = null;

    try {
      result = await invoke('run_simulation', {
        scenario: {
          event_name: eventName,
          session: session,
          base_driver: focusedDriver,
          swap_car_with: swapCar || null,
          swap_driver_inputs_with: swapDriver || null,
          car_params: {
            engine_power_factor: enginePower,
            aero_downforce_factor: downforce,
            aero_drag_factor: drag,
            tyre_compound: compound || null,
            tyre_wear_rate: tyreWear,
            fuel_load_kg: fuelLoad,
          },
          env_params: {
            track_temp_c: trackTemp,
            air_temp_c: 24.0,
            wind_speed_ms: windSpeed,
            wind_direction_deg: windDir,
            humidity: 0.4,
          },
          num_laps: null,
        }
      });
    } catch (e: any) {
      error = String(e);
    } finally {
      isRunning = false;
    }
  }

  function reset() {
    enginePower = 1.0; downforce = 1.0; drag = 1.0;
    compound = 'HARD'; tyreWear = 1.0; fuelLoad = 95;
    windSpeed = 0; windDir = 0; trackTemp = 35;
    swapCar = ''; swapDriver = '';
    result = null; error = '';
  }

  // SVG sparkline for lap deltas
  function sparkline(deltas: number[]): string {
    if (!deltas.length) return '';
    const maxAbs = Math.max(...deltas.map(Math.abs), 0.001);
    const w = 200, h = 40, mid = h / 2;
    const pts = deltas.map((d, i) => {
      const x = (i / (deltas.length - 1)) * w;
      const y = mid - (d / maxAbs) * (mid - 4);
      return `${x},${y}`;
    });
    return pts.join(' ');
  }

  $: lapDeltas = result?.laps?.map((l: any) => l.delta_to_baseline_s) ?? [];
  $: totalDelta = result?.delta_summary?.total_time_delta_s ?? 0;
  $: avgDelta = result?.delta_summary?.avg_lap_delta_s ?? 0;
  $: fmtDelta = (s: number) => `${s >= 0 ? '+' : ''}${s.toFixed(3)}s`;
</script>

<div class="sim-panel">
  <div class="sim-title">WHAT IF</div>

  {#if !focusedDriver}
    <p class="hint">Select a driver in the leaderboard to run simulations</p>
  {:else}
    <div class="sections">
      <!-- Car Params -->
      <div class="section">
        <div class="section-title">CAR SETUP</div>
        <div class="slider-row">
          <label>Engine {(enginePower * 100).toFixed(0)}%</label>
          <input type="range" min="0.8" max="1.2" step="0.01" bind:value={enginePower} />
        </div>
        <div class="slider-row">
          <label>Downforce {(downforce * 100).toFixed(0)}%</label>
          <input type="range" min="0.7" max="1.3" step="0.01" bind:value={downforce} />
        </div>
        <div class="slider-row">
          <label>Drag {(drag * 100).toFixed(0)}%</label>
          <input type="range" min="0.7" max="1.3" step="0.01" bind:value={drag} />
        </div>
        <div class="slider-row">
          <label>Fuel {fuelLoad}kg</label>
          <input type="range" min="60" max="110" step="1" bind:value={fuelLoad} />
        </div>
        <div class="select-row">
          <label>Tyre</label>
          <select bind:value={compound}>
            {#each compounds as c}
              <option value={c}>{c}</option>
            {/each}
          </select>
        </div>
      </div>

      <!-- Environment -->
      <div class="section">
        <div class="section-title">ENVIRONMENT</div>
        <div class="slider-row">
          <label>Track Temp {trackTemp}°C</label>
          <input type="range" min="10" max="60" step="1" bind:value={trackTemp} />
        </div>
        <div class="slider-row">
          <label>Wind {windSpeed} m/s</label>
          <input type="range" min="0" max="20" step="0.5" bind:value={windSpeed} />
        </div>
        <div class="slider-row">
          <label>Wind Dir {windDir}°</label>
          <input type="range" min="0" max="360" step="5" bind:value={windDir} />
        </div>
      </div>

      <!-- Swaps -->
      <div class="section">
        <div class="section-title">DRIVER/CAR SWAP</div>
        <div class="select-row">
          <label>Use car of</label>
          <select bind:value={swapCar}>
            <option value="">None</option>
            {#each driverMeta.filter(d => d.driver_number !== focusedDriver) as d}
              <option value={d.driver_number}>{d.abbreviation}</option>
            {/each}
          </select>
        </div>
        <div class="select-row">
          <label>Drive like</label>
          <select bind:value={swapDriver}>
            <option value="">None</option>
            {#each driverMeta.filter(d => d.driver_number !== focusedDriver) as d}
              <option value={d.driver_number}>{d.abbreviation}</option>
            {/each}
          </select>
        </div>
      </div>
    </div>

    <div class="actions">
      <button class="run-btn" on:click={runSim} disabled={isRunning}>
        {isRunning ? 'Running...' : 'Run Simulation'}
      </button>
      <button class="reset-btn" on:click={reset}>Reset</button>
    </div>

    {#if error}
      <div class="error">{error}</div>
    {/if}

    {#if result}
      <div class="results">
        <div class="result-header">SIMULATION RESULTS</div>
        <div class="result-stats">
          <div class="stat">
            <span class="stat-label">Total Delta</span>
            <span class="stat-val" class:positive={totalDelta < 0} class:negative={totalDelta > 0}>
              {fmtDelta(totalDelta)}
            </span>
          </div>
          <div class="stat">
            <span class="stat-label">Avg Lap Delta</span>
            <span class="stat-val" class:positive={avgDelta < 0} class:negative={avgDelta > 0}>
              {fmtDelta(avgDelta)}
            </span>
          </div>
          <div class="stat">
            <span class="stat-label">Fastest</span>
            <span class="stat-val">{result.fastest_lap_s?.toFixed(3)}s</span>
          </div>
        </div>

        {#if lapDeltas.length > 1}
          <div class="sparkline-container">
            <div class="spark-label">Lap time delta</div>
            <svg width="200" height="40" viewBox="0 0 200 40">
              <line x1="0" y1="20" x2="200" y2="20" stroke="rgba(255,255,255,0.1)" />
              <polyline
                points={sparkline(lapDeltas)}
                fill="none"
                stroke={totalDelta < 0 ? '#00ee44' : '#ff3300'}
                stroke-width="1.5"
              />
            </svg>
          </div>
        {/if}

        <div class="description">{result.delta_summary?.description}</div>
      </div>
    {/if}
  {/if}
</div>

<style>
  .sim-panel {
    padding: 12px;
    background: rgba(0,0,0,0.4);
    border-left: 1px solid rgba(255,255,255,0.06);
    width: 260px; flex-shrink: 0;
    overflow-y: auto;
    display: flex; flex-direction: column; gap: 8px;
  }
  .sim-title { font-size: 10px; font-weight: 700; letter-spacing: 0.15em; color: #ff8000; }
  .hint { font-size: 11px; color: rgba(255,255,255,0.3); }
  .sections { display: flex; flex-direction: column; gap: 12px; }
  .section { display: flex; flex-direction: column; gap: 4px; }
  .section-title { font-size: 9px; letter-spacing: 0.12em; color: rgba(255,255,255,0.35); margin-bottom: 4px; }
  .slider-row { display: flex; align-items: center; gap: 6px; }
  .slider-row label { font-size: 10px; color: rgba(255,255,255,0.6); width: 90px; flex-shrink: 0; }
  .slider-row input[type=range] { flex: 1; height: 3px; accent-color: #ff8000; cursor: pointer; }
  .select-row { display: flex; align-items: center; gap: 6px; }
  .select-row label { font-size: 10px; color: rgba(255,255,255,0.6); width: 90px; }
  .select-row select {
    flex: 1; background: rgba(255,255,255,0.06); color: white;
    border: 1px solid rgba(255,255,255,0.12); border-radius: 3px;
    padding: 2px 4px; font-size: 10px; font-family: inherit;
  }
  .actions { display: flex; gap: 6px; }
  .run-btn {
    flex: 1; padding: 6px; background: rgba(255,128,0,0.2);
    border: 1px solid rgba(255,128,0,0.5); color: #ff8000;
    border-radius: 4px; font-family: inherit; font-size: 11px;
    cursor: pointer; font-weight: 600;
  }
  .run-btn:hover:not(:disabled) { background: rgba(255,128,0,0.35); }
  .run-btn:disabled { opacity: 0.5; cursor: not-allowed; }
  .reset-btn {
    padding: 6px 10px; background: rgba(255,255,255,0.05);
    border: 1px solid rgba(255,255,255,0.1); color: rgba(255,255,255,0.5);
    border-radius: 4px; font-family: inherit; font-size: 11px; cursor: pointer;
  }
  .error { color: #ff4444; font-size: 11px; }
  .results { display: flex; flex-direction: column; gap: 8px; margin-top: 4px; border-top: 1px solid rgba(255,255,255,0.08); padding-top: 8px; }
  .result-header { font-size: 9px; letter-spacing: 0.12em; color: rgba(255,255,255,0.35); }
  .result-stats { display: flex; gap: 12px; }
  .stat { display: flex; flex-direction: column; gap: 2px; }
  .stat-label { font-size: 9px; color: rgba(255,255,255,0.35); }
  .stat-val { font-size: 13px; font-weight: 700; }
  .positive { color: #00ee44; }
  .negative { color: #ff3300; }
  .sparkline-container { display: flex; flex-direction: column; gap: 2px; }
  .spark-label { font-size: 9px; color: rgba(255,255,255,0.35); }
  .description { font-size: 10px; color: rgba(255,255,255,0.5); line-height: 1.4; }
</style>
