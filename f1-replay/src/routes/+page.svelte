<script lang="ts">
  import { onMount } from 'svelte';
  import { SceneManager } from '$lib/scene/SceneManager';
  import { TrackRenderer } from '$lib/scene/TrackRenderer';
  import { CarRenderer } from '$lib/scene/CarRenderer';
  import type { DriverFrame, DriverMeta, SessionInfo, TrackLayout } from '$lib/commands';
  import {
    getSessions,
    loadSessionCmd,
    getSpeedHeatmap,
    getFrame,
    getDriverMeta,
  } from '$lib/commands';
  import EventSelector from '$lib/components/EventSelector.svelte';
  import Leaderboard from '$lib/components/Leaderboard.svelte';
  import TelemetryPanel from '$lib/components/TelemetryPanel.svelte';
  import PlaybackControls from '$lib/components/PlaybackControls.svelte';
  import SimulationPanel from '$lib/components/SimulationPanel.svelte';
  import ComparisonPanel from '$lib/components/ComparisonPanel.svelte';

  let canvas: HTMLCanvasElement;
  let containerEl: HTMLDivElement;

  const sm = new SceneManager();
  const trackRenderer = new TrackRenderer();
  const carRenderer = new CarRenderer();

  // ── State ─────────────────────────────────────────────────────────────────
  let sessions: SessionInfo[] = [];
  let selectedSession: SessionInfo | null = null;
  let layout: TrackLayout | null = null;
  let driverMeta: DriverMeta[] = [];
  let teamMap: Record<string, string> = {};
  let abbrMap: Record<string, string> = {};

  let isLoading = false;
  let loadingMsg = '';
  let loadError = '';

  // Playback
  let isPlaying = false;
  let playbackSpeed = 1;
  let currentTime = 0;
  let duration = 0;

  // Frame data
  let currentDrivers: DriverFrame[] = [];
  let focusedDriver: string | null = null;

  // RAF state
  let rafId = 0;
  let lastRafTime = 0;
  let frameInFlight = false;

  // Right panel tab
  type RightTab = 'whatif' | 'compare';
  let rightTab: RightTab = 'whatif';

  // ── Lifecycle ─────────────────────────────────────────────────────────────
  onMount(() => {
    // Init with actual container dimensions (layout is ready by onMount)
    const r = containerEl.getBoundingClientRect();
    sm.init(canvas, r.width || 1200, r.height || 800);
    carRenderer.init(sm.scene, sm);

    // Resize observer — keeps canvas in sync with container
    const ro = new ResizeObserver((entries) => {
      const { width, height } = entries[0].contentRect;
      if (width > 0 && height > 0) sm.resize(width, height);
    });
    ro.observe(containerEl);

    // Load session list
    getSessions()
      .then((s) => {
        sessions = s;
        // Auto-load Las Vegas GP Race
        const lvgp = s.find(
          (x) => x.event_name.toLowerCase().includes('las vegas') && x.session === 'R'
        );
        if (lvgp) loadSession(lvgp);
      })
      .catch((e) => console.error('getSessions failed:', e));

    // Start animation loop
    lastRafTime = performance.now();
    rafId = requestAnimationFrame(animLoop);

    return () => {
      cancelAnimationFrame(rafId);
      ro.disconnect();
    };
  });

  // ── Load session ──────────────────────────────────────────────────────────
  async function loadSession(info: SessionInfo) {
    if (isLoading) return;
    isLoading = true;
    loadError = '';
    loadingMsg = 'Loading telemetry data — this takes ~30s first load…';
    selectedSession = info;
    currentDrivers = [];

    try {
      layout = await loadSessionCmd(info.event_name, info.session);   // camelCase handled inside loadSessionCmd
      sm.setTrackBounds(layout.x_min, layout.x_max, layout.y_min, layout.y_max);
      duration = layout.duration_s;
      currentTime = 0;
      isPlaying = false;

      loadingMsg = 'Building speed heatmap…';
      const heatmap = await getSpeedHeatmap();
      trackRenderer.buildHeatmap(heatmap, sm.scene, sm);
      trackRenderer.buildTrackOutline(layout.center_line, sm.scene, sm);

      loadingMsg = 'Setting up drivers…';
      driverMeta = await getDriverMeta();
      teamMap = Object.fromEntries(driverMeta.map((d) => [d.driver_number, d.team]));
      abbrMap = Object.fromEntries(driverMeta.map((d) => [d.driver_number, d.abbreviation]));
      carRenderer.setupDrivers(driverMeta.map((d) => d.driver_number), teamMap);

      // ── Fetch initial frame so cars appear immediately ──────────────────
      loadingMsg = 'Rendering first frame…';
      const fd = await getFrame(0);
      currentDrivers = fd.drivers;
      carRenderer.update(fd.drivers);

    } catch (e: any) {
      loadError = String(e);
      console.error('loadSession failed:', e);
    } finally {
      isLoading = false;
    }
  }

  // ── Animation loop ────────────────────────────────────────────────────────
  function animLoop(now: number) {
    rafId = requestAnimationFrame(animLoop);
    const dt = Math.min((now - lastRafTime) / 1000, 0.1);
    lastRafTime = now;

    if (isPlaying && !frameInFlight && layout) {
      currentTime = Math.min(currentTime + dt * playbackSpeed, duration);
      if (currentTime >= duration) {
        isPlaying = false;
      }

      frameInFlight = true;
      getFrame(currentTime)
        .then((fd) => {
          currentDrivers = fd.drivers;
          carRenderer.update(fd.drivers);
          if (focusedDriver) carRenderer.focusOnDriver(focusedDriver, sm);
          frameInFlight = false;
        })
        .catch(() => {
          frameInFlight = false;
        });
    }

    sm.render();
  }

  // ── Seek (scrubber) ───────────────────────────────────────────────────────
  function handleSeek(e: CustomEvent<number>) {
    currentTime = e.detail;
    if (!frameInFlight && layout) {
      frameInFlight = true;
      getFrame(currentTime)
        .then((fd) => {
          currentDrivers = fd.drivers;
          carRenderer.update(fd.drivers);
          frameInFlight = false;
        })
        .catch(() => {
          frameInFlight = false;
        });
    }
  }

  // ── Driver focus ──────────────────────────────────────────────────────────
  function handleDriverClick(e: CustomEvent<string>) {
    focusedDriver = focusedDriver === e.detail ? null : e.detail;
  }
</script>

<div class="app">
  <!-- Header -->
  <header>
    <div class="logo">F1 REPLAY</div>
    {#if sessions.length > 0}
      <EventSelector {sessions} on:select={(e) => loadSession(e.detail)} />
    {/if}
    <div class="header-right">
      {#if selectedSession}
        <span class="event-name">
          {selectedSession.event_name} · {selectedSession.session}
        </span>
      {/if}
    </div>
  </header>

  <!-- Main content -->
  <div class="main">
    <!-- Leaderboard sidebar -->
    <aside>
      <Leaderboard
        drivers={currentDrivers}
        {abbrMap}
        {teamMap}
        {focusedDriver}
        on:click={handleDriverClick}
      />
    </aside>

    <!-- Three.js canvas -->
    <div class="canvas-container" bind:this={containerEl}>
      <canvas bind:this={canvas}></canvas>
      {#if isLoading}
        <div class="loading-overlay">
          <div class="spinner"></div>
          <p>{loadingMsg}</p>
        </div>
      {:else if loadError}
        <div class="loading-overlay">
          <p class="err">⚠ {loadError}</p>
        </div>
      {/if}
    </div>

    <!-- Right panel: What-If / Compare tabs -->
    <div class="right-panel">
      <div class="tab-bar">
        <button
          class="tab-btn"
          class:active={rightTab === 'whatif'}
          on:click={() => rightTab = 'whatif'}
        >WHAT IF</button>
        <button
          class="tab-btn"
          class:active={rightTab === 'compare'}
          on:click={() => rightTab = 'compare'}
        >COMPARE</button>
      </div>
      <div class="tab-content">
        {#if rightTab === 'whatif'}
          <SimulationPanel
            {driverMeta}
            {focusedDriver}
            eventName={selectedSession?.event_name ?? ''}
            session={selectedSession?.session ?? ''}
          />
        {:else}
          <ComparisonPanel {driverMeta} />
        {/if}
      </div>
    </div>
  </div>

  <!-- Footer controls -->
  <footer>
    <TelemetryPanel
      drivers={currentDrivers}
      {focusedDriver}
      {abbrMap}
      {teamMap}
    />
    <PlaybackControls
      bind:isPlaying
      bind:playbackSpeed
      bind:currentTime
      {duration}
      on:seek={handleSeek}
    />
  </footer>
</div>

<style>
  :global(*, *::before, *::after) {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
  }
  :global(body) {
    background: #03010a;
    color: #e0e0e0;
    font-family: 'JetBrains Mono', 'Fira Code', 'Cascadia Code', monospace;
    overflow: hidden;
  }

  .app {
    display: flex;
    flex-direction: column;
    height: 100vh;
    width: 100vw;
    overflow: hidden;
  }

  header {
    display: flex;
    align-items: center;
    gap: 16px;
    height: 48px;
    padding: 0 16px;
    background: rgba(0, 0, 0, 0.6);
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
    flex-shrink: 0;
    z-index: 10;
  }
  .logo {
    font-size: 16px;
    font-weight: 700;
    letter-spacing: 0.12em;
    color: #ff8000;
    flex-shrink: 0;
  }
  .header-right {
    margin-left: auto;
  }
  .event-name {
    font-size: 12px;
    color: rgba(255, 255, 255, 0.5);
    letter-spacing: 0.05em;
  }

  .main {
    display: flex;
    flex: 1;
    overflow: hidden;
    min-height: 0;
  }

  aside {
    width: 180px;
    flex-shrink: 0;
    background: rgba(0, 0, 0, 0.4);
    border-right: 1px solid rgba(255, 255, 255, 0.06);
    overflow-y: auto;
  }

  .canvas-container {
    flex: 1;
    position: relative;
    overflow: hidden;
  }
  canvas {
    display: block;
    width: 100%;
    height: 100%;
  }

  /* Right panel */
  .right-panel {
    width: 280px;
    flex-shrink: 0;
    background: rgba(0, 0, 0, 0.4);
    border-left: 1px solid rgba(255, 255, 255, 0.06);
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }
  .tab-bar {
    display: flex;
    flex-shrink: 0;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  }
  .tab-btn {
    flex: 1;
    padding: 8px 4px;
    background: none;
    border: none;
    color: rgba(255, 255, 255, 0.35);
    font-family: inherit;
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 0.12em;
    cursor: pointer;
    border-bottom: 2px solid transparent;
    transition: color 0.15s, border-color 0.15s;
  }
  .tab-btn:hover {
    color: rgba(255, 255, 255, 0.7);
  }
  .tab-btn.active {
    color: #ff8000;
    border-bottom-color: #ff8000;
  }
  .tab-btn.active:last-child {
    color: #27F4D2;
    border-bottom-color: #27F4D2;
  }
  .tab-content {
    flex: 1;
    overflow: hidden;
    display: flex;
    flex-direction: column;
  }

  .loading-overlay {
    position: absolute;
    inset: 0;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    background: rgba(3, 1, 10, 0.85);
    gap: 16px;
    z-index: 20;
  }
  .spinner {
    width: 40px;
    height: 40px;
    border: 3px solid rgba(255, 128, 0, 0.3);
    border-top-color: #ff8000;
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
  }
  @keyframes spin {
    to {
      transform: rotate(360deg);
    }
  }
  .loading-overlay p {
    font-size: 13px;
    color: rgba(255, 255, 255, 0.6);
  }
  .loading-overlay p.err {
    color: #ff5555;
    max-width: 400px;
    text-align: center;
    line-height: 1.5;
  }

  footer {
    display: flex;
    align-items: center;
    height: 100px;
    flex-shrink: 0;
    background: rgba(0, 0, 0, 0.6);
    border-top: 1px solid rgba(255, 255, 255, 0.08);
    padding: 0 16px;
    gap: 24px;
    overflow: hidden;
  }
</style>
