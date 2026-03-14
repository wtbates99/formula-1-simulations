<script lang="ts">
  import { createEventDispatcher } from 'svelte';

  export let isPlaying = false;
  export let playbackSpeed = 1;
  export let currentTime = 0;
  export let duration = 8459;

  const dispatch = createEventDispatcher<{ seek: number }>();

  const speeds = [0.25, 0.5, 1, 2, 4, 8, 16];

  function fmt(s: number): string {
    const m = Math.floor(s / 60);
    const sec = Math.floor(s % 60);
    return `${m.toString().padStart(2, '0')}:${sec.toString().padStart(2, '0')}`;
  }

  function onScrub(e: Event) {
    const val = parseFloat((e.target as HTMLInputElement).value);
    dispatch('seek', val);
  }
</script>

<div class="controls">
  <button
    class="play-btn"
    on:click={() => (isPlaying = !isPlaying)}
    title={isPlaying ? 'Pause' : 'Play'}
  >
    {isPlaying ? '⏸' : '▶'}
  </button>

  <div class="speed-btns">
    {#each speeds as s}
      <button
        class:active={playbackSpeed === s}
        on:click={() => (playbackSpeed = s)}
      >{s}×</button>
    {/each}
  </div>

  <div class="scrub">
    <span class="time">{fmt(currentTime)}</span>
    <input
      type="range"
      min="0"
      max={duration}
      step="0.1"
      value={currentTime}
      on:input={onScrub}
    />
    <span class="time">{fmt(duration)}</span>
  </div>
</div>

<style>
  .controls {
    display: flex;
    align-items: center;
    gap: 16px;
    flex: 1;
  }
  .play-btn {
    width: 40px;
    height: 40px;
    border-radius: 50%;
    background: rgba(255, 128, 0, 0.2);
    border: 1px solid rgba(255, 128, 0, 0.5);
    color: #ff8000;
    font-size: 16px;
    cursor: pointer;
    display: flex;
    align-items: center;
    justify-content: center;
    transition: background 0.1s;
    flex-shrink: 0;
  }
  .play-btn:hover {
    background: rgba(255, 128, 0, 0.35);
  }
  .speed-btns {
    display: flex;
    gap: 4px;
    flex-shrink: 0;
  }
  .speed-btns button {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.1);
    color: rgba(255, 255, 255, 0.5);
    font-size: 10px;
    font-family: inherit;
    padding: 3px 7px;
    border-radius: 3px;
    cursor: pointer;
    transition: all 0.1s;
  }
  .speed-btns button.active {
    background: rgba(255, 128, 0, 0.25);
    border-color: #ff8000;
    color: #ff8000;
  }
  .speed-btns button:hover {
    background: rgba(255, 255, 255, 0.1);
  }
  .scrub {
    display: flex;
    align-items: center;
    gap: 8px;
    flex: 1;
  }
  .time {
    font-size: 11px;
    color: rgba(255, 255, 255, 0.5);
    min-width: 40px;
    flex-shrink: 0;
  }
  input[type='range'] {
    flex: 1;
    height: 4px;
    accent-color: #ff8000;
    cursor: pointer;
    background: rgba(255, 255, 255, 0.1);
    border-radius: 2px;
  }
</style>
