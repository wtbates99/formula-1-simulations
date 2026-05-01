<script lang="ts">
  import { onMount } from 'svelte';
  import { getRaceAnalysis, type RaceAnalysis, type DriverAnalysis } from '$lib/commands';
  import { TEAM_COLORS_HEX } from '$lib/constants';

  export let refreshKey = '';

  let analysis: RaceAnalysis | null = null;
  let isLoading = false;
  let error = '';

  $: if (refreshKey) {
    loadAnalysis();
  }

  onMount(loadAnalysis);

  async function loadAnalysis() {
    if (isLoading) return;
    isLoading = true;
    error = '';
    try {
      analysis = await getRaceAnalysis();
    } catch (e: any) {
      error = String(e);
    } finally {
      isLoading = false;
    }
  }

  function fmtTime(s: number | null): string {
    if (s === null || !Number.isFinite(s)) return '--';
    const m = Math.floor(s / 60);
    return `${m}:${(s % 60).toFixed(3).padStart(6, '0')}`;
  }

  function signed(n: number): string {
    return `${n > 0 ? '+' : ''}${n}`;
  }

  function color(d: DriverAnalysis): string {
    return TEAM_COLORS_HEX[d.team] ?? '#888888';
  }
</script>

<div class="analysis-panel">
  <div class="panel-head">
    <div>
      <div class="title">RACE ANALYSIS</div>
      {#if analysis}
        <div class="subtitle">{analysis.valid_lap_count} clean laps · {analysis.driver_count} drivers</div>
      {/if}
    </div>
    <button on:click={loadAnalysis} disabled={isLoading}>{isLoading ? '...' : 'RUN'}</button>
  </div>

  {#if error}
    <div class="error">{error}</div>
  {:else if analysis}
    <div class="summary-grid">
      <div class="metric">
        <span>Fastest</span>
        <strong>{fmtTime(analysis.fastest_lap_s)}</strong>
      </div>
      <div class="metric">
        <span>Median Pace</span>
        <strong>{fmtTime(analysis.median_race_pace_s)}</strong>
      </div>
    </div>

    <div class="section">
      <div class="section-title">INSIGHTS</div>
      {#each analysis.insights.slice(0, 5) as insight}
        <div class="insight">
          <div class="insight-kind">{insight.kind}</div>
          <div class="insight-title">{insight.title}</div>
          <div class="insight-detail">{insight.detail}</div>
        </div>
      {/each}
    </div>

    <div class="section">
      <div class="section-title">POWER RANKING</div>
      <div class="driver-table">
        {#each analysis.drivers.slice(0, 10) as d, i}
          <div class="driver-row" style="--team:{color(d)}">
            <span class="rank">{i + 1}</span>
            <span class="team"></span>
            <span class="abbr">{d.abbreviation}</span>
            <span class="score">{d.performance_score.toFixed(1)}</span>
            <span class="pace">{fmtTime(d.median_lap_s)}</span>
            <span class="gain">{signed(d.positions_gained)}</span>
          </div>
        {/each}
      </div>
    </div>

    <div class="section">
      <div class="section-title">STINTS</div>
      <div class="stints">
        {#each analysis.stints.slice(0, 8) as stint}
          <div class="stint">
            <span class="stint-driver">{stint.driver_number}</span>
            <span>{stint.compound}</span>
            <span>L{stint.start_lap}-{stint.end_lap}</span>
            <span>{fmtTime(stint.avg_lap_s)}</span>
          </div>
        {/each}
      </div>
    </div>
  {:else}
    <div class="hint">Load a session to run race analysis.</div>
  {/if}
</div>

<style>
  .analysis-panel {
    width: 100%;
    height: 100%;
    padding: 12px;
    display: flex;
    flex-direction: column;
    gap: 12px;
    overflow-y: auto;
  }
  .panel-head {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    gap: 12px;
  }
  .title {
    font-size: 10px;
    font-weight: 800;
    letter-spacing: 0.14em;
    color: #ffe100;
  }
  .subtitle {
    margin-top: 3px;
    font-size: 9px;
    color: rgba(255, 255, 255, 0.35);
  }
  button {
    background: rgba(255, 225, 0, 0.1);
    border: 1px solid rgba(255, 225, 0, 0.35);
    border-radius: 3px;
    color: #ffe100;
    cursor: pointer;
    font-family: inherit;
    font-size: 10px;
    font-weight: 800;
    padding: 4px 8px;
  }
  button:disabled {
    cursor: not-allowed;
    opacity: 0.45;
  }
  .summary-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 8px;
  }
  .metric {
    background: rgba(255, 255, 255, 0.045);
    border: 1px solid rgba(255, 255, 255, 0.07);
    border-radius: 4px;
    padding: 8px;
  }
  .metric span,
  .section-title,
  .insight-kind {
    color: rgba(255, 255, 255, 0.35);
    font-size: 8px;
    letter-spacing: 0.1em;
  }
  .metric strong {
    display: block;
    margin-top: 4px;
    font-size: 15px;
  }
  .section {
    display: flex;
    flex-direction: column;
    gap: 6px;
  }
  .insight {
    border-left: 2px solid rgba(255, 225, 0, 0.65);
    background: rgba(255, 255, 255, 0.035);
    padding: 7px 8px;
  }
  .insight-title {
    margin-top: 3px;
    font-size: 12px;
    font-weight: 800;
  }
  .insight-detail {
    margin-top: 3px;
    color: rgba(255, 255, 255, 0.48);
    font-size: 10px;
    line-height: 1.35;
  }
  .driver-table,
  .stints {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .driver-row {
    display: grid;
    grid-template-columns: 18px 6px 1fr 42px 62px 28px;
    align-items: center;
    gap: 6px;
    min-height: 24px;
    color: rgba(255, 255, 255, 0.72);
    font-size: 10px;
  }
  .rank {
    color: rgba(255, 255, 255, 0.35);
  }
  .team {
    width: 6px;
    height: 14px;
    border-radius: 1px;
    background: var(--team);
  }
  .abbr,
  .score {
    font-weight: 800;
  }
  .pace,
  .gain {
    color: rgba(255, 255, 255, 0.4);
    text-align: right;
  }
  .stint {
    display: grid;
    grid-template-columns: 34px 1fr 58px 58px;
    gap: 6px;
    font-size: 10px;
    color: rgba(255, 255, 255, 0.58);
  }
  .stint-driver {
    font-weight: 800;
    color: rgba(255, 255, 255, 0.78);
  }
  .error {
    color: #ff5544;
    font-size: 10px;
    line-height: 1.4;
  }
  .hint {
    color: rgba(255, 255, 255, 0.3);
    font-size: 11px;
  }
</style>
