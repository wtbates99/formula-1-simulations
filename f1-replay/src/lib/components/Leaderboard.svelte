<script lang="ts">
  import { createEventDispatcher } from 'svelte';
  import type { DriverFrame } from '$lib/commands';
  import { TEAM_COLORS_HEX, COMPOUND_COLORS } from '$lib/constants';

  export let drivers: DriverFrame[] = [];
  export let abbrMap: Record<string, string> = {};
  export let teamMap: Record<string, string> = {};
  export let focusedDriver: string | null = null;

  const dispatch = createEventDispatcher<{ click: string }>();

  $: sorted = [...drivers].sort((a, b) => a.position - b.position);

  function teamColor(num: string): string {
    return TEAM_COLORS_HEX[teamMap[num]] ?? '#888888';
  }

  function compoundColor(compound: string): string {
    return COMPOUND_COLORS[compound] ?? '#888888';
  }
</script>

<div class="lb">
  <div class="lb-title">RACE ORDER</div>
  {#each sorted as d (d.driver_number)}
    <button
      class="row"
      class:focused={focusedDriver === d.driver_number}
      on:click={() => dispatch('click', d.driver_number)}
    >
      <span class="pos">{d.position}</span>
      <span
        class="dot"
        style="background: {teamColor(d.driver_number)}"
      ></span>
      <span class="abbr">{abbrMap[d.driver_number] ?? d.driver_number}</span>
      <span
        class="tyre"
        style="color: {compoundColor(d.compound)}"
        title={d.compound}
      >{d.compound[0]}</span>
    </button>
  {/each}
</div>

<style>
  .lb {
    padding: 8px 0;
  }
  .lb-title {
    font-size: 9px;
    letter-spacing: 0.12em;
    color: rgba(255, 255, 255, 0.35);
    padding: 4px 12px 8px;
  }
  .row {
    display: flex;
    align-items: center;
    gap: 6px;
    width: 100%;
    padding: 5px 12px;
    background: none;
    border: none;
    border-left: 2px solid transparent;
    color: rgba(255, 255, 255, 0.7);
    font-family: inherit;
    font-size: 12px;
    cursor: pointer;
    transition: background 0.1s;
    text-align: left;
  }
  .row:hover {
    background: rgba(255, 255, 255, 0.06);
  }
  .row.focused {
    background: rgba(255, 128, 0, 0.15);
    border-left: 2px solid #ff8000;
  }
  .pos {
    width: 16px;
    color: rgba(255, 255, 255, 0.4);
    font-size: 10px;
    flex-shrink: 0;
  }
  .dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    flex-shrink: 0;
  }
  .abbr {
    flex: 1;
    font-weight: 600;
    letter-spacing: 0.05em;
  }
  .tyre {
    font-size: 10px;
    font-weight: 700;
  }
</style>
