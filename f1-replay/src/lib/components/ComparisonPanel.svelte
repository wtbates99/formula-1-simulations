<script lang="ts">
  import { invoke } from '@tauri-apps/api/core';
  import { compareLapsCmd } from '$lib/commands';
  import { TEAM_COLORS_HEX } from '$lib/constants';
  import TelemetryChart from './TelemetryChart.svelte';
  import type { LapComparison } from '$lib/commands';

  export let driverMeta: Array<{driver_number: string, abbreviation: string, team: string}> = [];

  let driverA = '';
  let driverB = '';
  let isLoading = false;
  let result: LapComparison | null = null;
  let error = '';
  let showChart = false;

  async function compare() {
    if (!driverA || !driverB || driverA === driverB) {
      error = 'Select two different drivers';
      return;
    }
    isLoading = true; error = ''; result = null; showChart = false;
    try {
      result = await compareLapsCmd(driverA, driverB);
      showChart = true;
    } catch (e: any) {
      // Fallback to lap-level comparison if distance-norm fails
      try {
        const r2 = await invoke<any>('compare_drivers_cmd', { driverA, driverB });
        error = `Distance comparison unavailable (${String(e).slice(0,60)}). Lap-level only.`;
        showChart = false;
      } catch (e2: any) {
        error = String(e);
      }
    } finally {
      isLoading = false;
    }
  }

  function meta(num: string) {
    return driverMeta.find(d => d.driver_number === num);
  }
  function color(num: string) {
    const m = meta(num);
    return m ? (TEAM_COLORS_HEX[m.team] ?? '#888') : '#888';
  }
  function abbr(num: string) {
    return meta(num)?.abbreviation ?? num;
  }
  function fmt(s: number) {
    const m = Math.floor(s / 60);
    return `${m}:${(s % 60).toFixed(3).padStart(6, '0')}`;
  }
  function fmtDelta(s: number) {
    return `${s >= 0 ? '+' : ''}${s.toFixed(3)}s`;
  }

  $: colorA = color(driverA);
  $: colorB = color(driverB);
  $: abbrA = abbr(driverA);
  $: abbrB = abbr(driverB);
</script>

<div class="panel">
  <div class="title">FASTEST LAP COMPARE</div>

  <div class="select-row">
    <select bind:value={driverA} style="border-color:{colorA}">
      <option value="">Driver A…</option>
      {#each driverMeta as d}
        <option value={d.driver_number}>{d.abbreviation}</option>
      {/each}
    </select>
    <span class="vs">vs</span>
    <select bind:value={driverB} style="border-color:{colorB}">
      <option value="">Driver B…</option>
      {#each driverMeta as d}
        <option value={d.driver_number}>{d.abbreviation}</option>
      {/each}
    </select>
    <button on:click={compare} disabled={isLoading}>
      {isLoading ? '…' : 'GO'}
    </button>
  </div>

  {#if error}
    <div class="error">{error}</div>
  {/if}

  {#if result}
    <!-- Stats row -->
    <div class="stats-row">
      <div class="stat-block" style="--c:{colorA}">
        <div class="abbr">{abbrA}</div>
        <div class="lap-time">{fmt(result.lap_time_a)}</div>
        <div class="stat-label">Lap {result.lap_number_a}</div>
      </div>
      <div class="delta-block">
        <div class="delta-val" class:delta-pos={result.lap_time_delta < 0} class:delta-neg={result.lap_time_delta > 0}>
          {fmtDelta(result.lap_time_delta)}
        </div>
        <div class="stat-label">A vs B</div>
      </div>
      <div class="stat-block right" style="--c:{colorB}">
        <div class="abbr">{abbrB}</div>
        <div class="lap-time">{fmt(result.lap_time_b)}</div>
        <div class="stat-label">Lap {result.lap_number_b}</div>
      </div>
    </div>

    <!-- Mini-sector strip -->
    {#if result.mini_sectors.length > 0}
      <div class="ms-section">
        <div class="ms-label">MINI SECTORS — {abbrA} (orange) vs {abbrB} (teal)</div>
        <div class="ms-strip">
          {#each result.mini_sectors as ms}
            <div
              class="ms-cell"
              style="background:{ms.delta_s < 0 ? colorB : (ms.delta_s > 0 ? colorA : 'rgba(255,255,255,0.08)')};
                     opacity:{Math.min(1.0, 0.3 + Math.abs(ms.delta_s) * 8)}"
              title="{ms.distance_start.toFixed(0)}–{ms.distance_end.toFixed(0)}m: {fmtDelta(ms.delta_s)}"
            ></div>
          {/each}
        </div>
      </div>
    {/if}

    <!-- Telemetry chart -->
    <div class="chart-area">
      <TelemetryChart data={result} {colorA} {colorB} />
    </div>
  {:else if !isLoading}
    <div class="hint">Compare fastest laps with full telemetry overlay.<br>Scroll/pinch chart to zoom.</div>
  {/if}
</div>

<style>
  .panel {
    width: 100%; height: 100%;
    padding: 10px;
    display: flex; flex-direction: column; gap: 8px;
    overflow: hidden;
  }
  .title {
    font-size: 9px; font-weight: 700; letter-spacing: 0.15em; color: #27F4D2;
    flex-shrink: 0;
  }
  .select-row {
    display: flex; align-items: center; gap: 5px; flex-shrink: 0;
  }
  .select-row select {
    flex: 1; background: rgba(255,255,255,0.05); color: white;
    border: 1px solid rgba(255,255,255,0.15); border-radius: 3px;
    padding: 3px 4px; font-size: 11px; font-family: inherit;
  }
  .vs { font-size: 9px; color: rgba(255,255,255,0.3); }
  .select-row button {
    padding: 4px 8px; background: rgba(39,244,210,0.08);
    border: 1px solid rgba(39,244,210,0.25); color: #27F4D2;
    border-radius: 3px; cursor: pointer; font-size: 10px;
    font-family: inherit; font-weight: 700; letter-spacing: 0.08em;
  }
  .select-row button:disabled { opacity: 0.4; cursor: not-allowed; }
  .error { color: #ff5544; font-size: 10px; flex-shrink: 0; }

  .stats-row {
    display: flex; align-items: center; gap: 6px; flex-shrink: 0;
    background: rgba(255,255,255,0.03); border-radius: 4px; padding: 8px;
  }
  .stat-block {
    flex: 1; display: flex; flex-direction: column; gap: 2px;
    color: var(--c, #888);
  }
  .stat-block.right { text-align: right; }
  .abbr { font-size: 15px; font-weight: 800; letter-spacing: 0.08em; }
  .lap-time { font-size: 11px; font-weight: 700; opacity: 0.9; }
  .stat-label { font-size: 8px; color: rgba(255,255,255,0.3); }
  .delta-block {
    display: flex; flex-direction: column; align-items: center; gap: 2px;
    min-width: 56px;
  }
  .delta-val {
    font-size: 12px; font-weight: 800; letter-spacing: 0.04em;
    color: rgba(255,255,255,0.5);
  }
  .delta-pos { color: #ff8000; } /* A is faster (negative delta = A faster? no: delta = lapA - lapB) */
  .delta-neg { color: #27F4D2; } /* B is faster */

  .ms-section { flex-shrink: 0; }
  .ms-label { font-size: 8px; color: rgba(255,255,255,0.25); margin-bottom: 3px; }
  .ms-strip {
    display: flex; height: 8px; border-radius: 2px; overflow: hidden;
    background: rgba(255,255,255,0.05);
  }
  .ms-cell { flex: 1; }

  .chart-area {
    flex: 1; min-height: 0;
    border-radius: 3px; overflow: hidden;
    border: 1px solid rgba(255,255,255,0.05);
  }

  .hint {
    font-size: 10px; color: rgba(255,255,255,0.2);
    line-height: 1.5; text-align: center; padding: 16px 8px;
  }
</style>
