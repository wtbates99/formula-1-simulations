<script lang="ts">
  import { invoke } from '@tauri-apps/api/core';
  import { TEAM_COLORS_HEX } from '$lib/constants';

  export let driverMeta: Array<{driver_number: string, abbreviation: string, team: string}> = [];

  let driverA = '';
  let driverB = '';
  let isLoading = false;
  let result: any = null;
  let error = '';

  async function compare() {
    if (!driverA || !driverB || driverA === driverB) {
      error = 'Select two different drivers';
      return;
    }
    isLoading = true; error = ''; result = null;
    try {
      result = await invoke('compare_drivers_cmd', { driverA, driverB });
    } catch (e: any) {
      error = String(e);
    } finally {
      isLoading = false;
    }
  }

  function colorA() {
    const m = driverMeta.find(d => d.driver_number === driverA);
    return m ? (TEAM_COLORS_HEX[m.team] ?? '#888') : '#888';
  }
  function colorB() {
    const m = driverMeta.find(d => d.driver_number === driverB);
    return m ? (TEAM_COLORS_HEX[m.team] ?? '#888') : '#888';
  }
  function abbrA() { return driverMeta.find(d => d.driver_number === driverA)?.abbreviation ?? driverA; }
  function abbrB() { return driverMeta.find(d => d.driver_number === driverB)?.abbreviation ?? driverB; }
  function fmt(s: number) { const m = Math.floor(s/60); return `${m}:${(s%60).toFixed(3).padStart(6,'0')}`; }

  // Sparkline of cumulative delta
  $: cumulativeDeltas = result?.lap_deltas?.map((l: any) => l.cumulative_delta_s) ?? [];
  function sparkCumulative(deltas: number[]): string {
    if (!deltas.length) return '';
    const min = Math.min(...deltas);
    const max = Math.max(...deltas);
    const range = Math.max(max - min, 0.001);
    const w = 220, h = 50;
    return deltas.map((d, i) => {
      const x = (i / (deltas.length - 1)) * w;
      const y = h - ((d - min) / range) * (h - 4) - 2;
      return `${x},${y}`;
    }).join(' ');
  }
</script>

<div class="comp-panel">
  <div class="comp-title">DRIVER COMPARE</div>

  <div class="select-row">
    <select bind:value={driverA} style="border-color: {colorA()}">
      <option value="">Driver A...</option>
      {#each driverMeta as d}
        <option value={d.driver_number}>{d.abbreviation}</option>
      {/each}
    </select>
    <span>vs</span>
    <select bind:value={driverB} style="border-color: {colorB()}">
      <option value="">Driver B...</option>
      {#each driverMeta as d}
        <option value={d.driver_number}>{d.abbreviation}</option>
      {/each}
    </select>
    <button on:click={compare} disabled={isLoading}>
      {isLoading ? '...' : 'GO'}
    </button>
  </div>

  {#if error}<div class="error">{error}</div>{/if}

  {#if result}
    <div class="stats-grid">
      <div class="stat-col" style="color:{colorA()}">
        <div class="abbr">{abbrA()}</div>
        <div class="val">{fmt(result.fastest_lap_a)}</div>
        <div class="label">Fastest Lap</div>
        <div class="val">{result.avg_lap_a?.toFixed(3)}s</div>
        <div class="label">Avg Lap</div>
        <div class="val">{result.max_speed_a?.toFixed(0)}</div>
        <div class="label">Max Speed</div>
      </div>
      <div class="divider"></div>
      <div class="stat-col" style="color:{colorB()}">
        <div class="abbr">{abbrB()}</div>
        <div class="val">{fmt(result.fastest_lap_b)}</div>
        <div class="label">Fastest Lap</div>
        <div class="val">{result.avg_lap_b?.toFixed(3)}s</div>
        <div class="label">Avg Lap</div>
        <div class="val">{result.max_speed_b?.toFixed(0)}</div>
        <div class="label">Max Speed</div>
      </div>
    </div>

    {#if cumulativeDeltas.length > 1}
      <div class="chart-section">
        <div class="chart-label">Cumulative gap (A−B), seconds</div>
        <svg width="220" height="50" viewBox="0 0 220 50">
          <line x1="0" y1="25" x2="220" y2="25" stroke="rgba(255,255,255,0.1)" />
          <polyline
            points={sparkCumulative(cumulativeDeltas)}
            fill="none"
            stroke={cumulativeDeltas[cumulativeDeltas.length-1] < 0 ? colorA() : colorB()}
            stroke-width="1.5"
          />
        </svg>
      </div>
    {/if}

    <div class="summary">{result.summary}</div>

    <!-- Lap-by-lap table (scrollable) -->
    {#if result.lap_deltas?.length}
      <div class="lap-table">
        <div class="lap-header">
          <span>Lap</span>
          <span style="color:{colorA()}">{abbrA()}</span>
          <span style="color:{colorB()}">{abbrB()}</span>
          <span>Delta</span>
        </div>
        {#each result.lap_deltas.slice(0, 20) as lap}
          <div class="lap-row">
            <span>{lap.lap_number}</span>
            <span>{lap.time_a?.toFixed(3)}</span>
            <span>{lap.time_b?.toFixed(3)}</span>
            <span class:positive={lap.delta_s < 0} class:negative={lap.delta_s > 0}>
              {lap.delta_s >= 0 ? '+' : ''}{lap.delta_s?.toFixed(3)}
            </span>
          </div>
        {/each}
      </div>
    {/if}
  {/if}
</div>

<style>
  .comp-panel {
    width: 100%; height: 100%;
    padding: 12px; overflow-y: auto;
    display: flex; flex-direction: column; gap: 10px;
  }
  .comp-title { font-size: 10px; font-weight: 700; letter-spacing: 0.15em; color: #27F4D2; }
  .select-row { display: flex; align-items: center; gap: 6px; }
  .select-row select {
    flex: 1; background: rgba(255,255,255,0.06); color: white;
    border: 1px solid; border-radius: 3px;
    padding: 3px 4px; font-size: 11px; font-family: inherit;
  }
  .select-row span { font-size: 10px; color: rgba(255,255,255,0.4); }
  .select-row button {
    padding: 4px 8px; background: rgba(255,255,255,0.08);
    border: 1px solid rgba(255,255,255,0.15); color: white;
    border-radius: 4px; cursor: pointer; font-size: 11px;
    font-family: inherit; font-weight: 600;
  }
  .error { color: #ff4444; font-size: 11px; }
  .stats-grid { display: flex; gap: 8px; align-items: flex-start; }
  .stat-col { flex: 1; display: flex; flex-direction: column; gap: 2px; text-align: center; }
  .abbr { font-size: 16px; font-weight: 800; letter-spacing: 0.1em; }
  .val { font-size: 11px; font-weight: 700; }
  .label { font-size: 9px; color: rgba(255,255,255,0.35); margin-bottom: 4px; }
  .divider { width: 1px; background: rgba(255,255,255,0.1); align-self: stretch; margin: 0 4px; }
  .chart-section { display: flex; flex-direction: column; gap: 3px; }
  .chart-label { font-size: 9px; color: rgba(255,255,255,0.35); }
  .summary { font-size: 11px; color: rgba(255,255,255,0.6); line-height: 1.4; }
  .lap-table { display: flex; flex-direction: column; gap: 1px; max-height: 200px; overflow-y: auto; }
  .lap-header { display: grid; grid-template-columns: 30px 1fr 1fr 1fr; gap: 4px; font-size: 9px; color: rgba(255,255,255,0.35); padding: 2px 4px; }
  .lap-row { display: grid; grid-template-columns: 30px 1fr 1fr 1fr; gap: 4px; font-size: 10px; padding: 2px 4px; }
  .lap-row:nth-child(even) { background: rgba(255,255,255,0.02); }
  .positive { color: #00ee44; }
  .negative { color: #ff3300; }
</style>
