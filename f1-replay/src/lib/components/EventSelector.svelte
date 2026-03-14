<script lang="ts">
  import { createEventDispatcher } from 'svelte';
  import type { SessionInfo } from '$lib/commands';

  export let sessions: SessionInfo[] = [];

  const dispatch = createEventDispatcher<{ select: SessionInfo }>();

  let selected = '';

  function onChange() {
    const s = sessions.find(
      (s) => `${s.event_name}||${s.session}` === selected
    );
    if (s) dispatch('select', s);
  }
</script>

<select bind:value={selected} on:change={onChange}>
  <option value="">Select race…</option>
  {#each sessions as s}
    <option value="{s.event_name}||{s.session}">
      {s.year ?? ''} {s.event_name} — {s.session}
    </option>
  {/each}
</select>

<style>
  select {
    background: rgba(255, 255, 255, 0.06);
    color: white;
    border: 1px solid rgba(255, 255, 255, 0.15);
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 12px;
    font-family: inherit;
    cursor: pointer;
    min-width: 260px;
  }
  select:focus {
    outline: 1px solid #ff8000;
  }
  option {
    background: #1a1a2e;
    color: white;
  }
</style>
