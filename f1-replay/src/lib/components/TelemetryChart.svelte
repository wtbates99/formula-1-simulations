<script lang="ts">
  import { onMount, afterUpdate } from 'svelte';
  import type { LapComparison } from '$lib/commands';
  import { TEAM_COLORS_HEX } from '$lib/constants';

  export let data: LapComparison | null = null;
  export let colorA = '#ff8000';
  export let colorB = '#27F4D2';

  let canvas: HTMLCanvasElement;
  let containerEl: HTMLDivElement;
  let ctx: CanvasRenderingContext2D;

  // Layout constants (fractions of total height)
  const CHANNELS = ['speed', 'throttle', 'brake', 'gear', 'delta'] as const;
  type Channel = typeof CHANNELS[number];

  const CHANNEL_HEIGHTS: Record<Channel, number> = {
    speed: 0.32,
    throttle: 0.18,
    brake: 0.18,
    gear: 0.14,
    delta: 0.18,
  };
  const PADDING_LEFT = 42;
  const PADDING_RIGHT = 8;
  const PADDING_TOP = 4;
  const PADDING_BOTTOM = 16;
  const CHANNEL_GAP = 2;

  // View state for horizontal zoom/pan
  let viewStart = 0.0;  // fraction of total distance
  let viewEnd   = 1.0;
  let cursorX: number | null = null;

  // Mouse
  let isDragging = false;
  let dragStartX = 0;
  let dragStartView = { start: 0, end: 1 };

  onMount(() => {
    const r = containerEl.getBoundingClientRect();
    canvas.width  = r.width  || 400;
    canvas.height = r.height || 240;
    ctx = canvas.getContext('2d')!;

    const ro = new ResizeObserver((entries) => {
      const { width, height } = entries[0].contentRect;
      if (width > 0 && height > 0) {
        canvas.width = width;
        canvas.height = height;
        draw();
      }
    });
    ro.observe(containerEl);
    draw();
    return () => ro.disconnect();
  });

  afterUpdate(() => { if (ctx) draw(); });

  function draw() {
    if (!ctx || !canvas) return;
    const cw = canvas.width;
    const ch = canvas.height;
    ctx.clearRect(0, 0, cw, ch);
    ctx.fillStyle = '#0f0f10';
    ctx.fillRect(0, 0, cw, ch);

    if (!data || data.distances.length === 0) {
      ctx.fillStyle = 'rgba(255,255,255,0.2)';
      ctx.font = '11px monospace';
      ctx.textAlign = 'center';
      ctx.fillText('Select two drivers and click COMPARE', cw / 2, ch / 2);
      return;
    }

    const plotW = cw - PADDING_LEFT - PADDING_RIGHT;
    const plotH = ch - PADDING_TOP - PADDING_BOTTOM;

    // Total channel height fractions must sum to 1.0
    const totalFrac = Object.values(CHANNEL_HEIGHTS).reduce((a, b) => a + b, 0);

    let yOffset = PADDING_TOP;
    for (const ch_name of CHANNELS) {
      const hFrac = CHANNEL_HEIGHTS[ch_name] / totalFrac;
      const hPx = Math.round(plotH * hFrac) - CHANNEL_GAP;
      drawChannel(ch_name, yOffset, plotW, hPx);
      yOffset += hPx + CHANNEL_GAP;
    }

    // Distance axis ticks
    drawDistanceAxis(yOffset, plotW);

    // Cursor
    if (cursorX !== null) {
      ctx.beginPath();
      ctx.moveTo(cursorX, PADDING_TOP);
      ctx.lineTo(cursorX, ch - PADDING_BOTTOM);
      ctx.strokeStyle = 'rgba(255,255,255,0.35)';
      ctx.lineWidth = 1;
      ctx.setLineDash([3, 3]);
      ctx.stroke();
      ctx.setLineDash([]);
    }
  }

  function drawChannel(
    name: Channel,
    y0: number,
    plotW: number,
    h: number,
  ) {
    if (!data) return;
    const cw = canvas.width;
    const x0 = PADDING_LEFT;
    const n = data.distances.length;
    const dMax = data.distances[n - 1];

    // Background
    ctx.fillStyle = 'rgba(255,255,255,0.025)';
    ctx.fillRect(x0, y0, plotW, h);

    // Channel label
    ctx.fillStyle = 'rgba(255,255,255,0.25)';
    ctx.font = '8px monospace';
    ctx.textAlign = 'right';
    ctx.textBaseline = 'top';
    const labels: Record<Channel, string> = {
      speed: 'SPD\nkm/h', throttle: 'THR', brake: 'BRK', gear: 'GR', delta: 'Δt(s)',
    };
    ctx.fillText(labels[name], x0 - 3, y0 + 1);

    // Get data arrays and y-scale
    let yA: number[];
    let yB: number[] | null = null;
    let yMin = 0;
    let yMax = 1;
    let showZeroLine = false;

    switch (name) {
      case 'speed':
        yA = data.speeds_a;
        yB = data.speeds_b;
        yMin = Math.min(...data.speeds_a, ...data.speeds_b) * 0.9;
        yMax = Math.max(...data.speeds_a, ...data.speeds_b) * 1.02;
        break;
      case 'throttle':
        yA = data.throttles_a;
        yB = data.throttles_b;
        yMin = 0; yMax = 1;
        break;
      case 'brake':
        yA = data.brakes_a;
        yB = data.brakes_b;
        yMin = 0; yMax = 1;
        break;
      case 'gear':
        yA = data.gears_a.map(Number);
        yB = data.gears_b.map(Number);
        yMin = 0; yMax = 8;
        break;
      case 'delta':
        yA = data.delta_time;
        yMin = Math.min(...data.delta_time);
        yMax = Math.max(...data.delta_time);
        const absMax = Math.max(Math.abs(yMin), Math.abs(yMax), 0.05);
        yMin = -absMax; yMax = absMax;
        showZeroLine = true;
        break;
    }

    // Clamp distance view
    const dStart = viewStart * dMax;
    const dEnd   = viewEnd   * dMax;
    const viewRange = dEnd - dStart;
    if (viewRange <= 0) return;

    const toPixX = (d: number) => x0 + ((d - dStart) / viewRange) * plotW;
    const toPixY = (v: number) => y0 + h - 1 - ((v - yMin) / (yMax - yMin || 1)) * (h - 2);

    // Zero line for delta
    if (showZeroLine) {
      const zy = toPixY(0);
      ctx.beginPath();
      ctx.moveTo(x0, zy);
      ctx.lineTo(x0 + plotW, zy);
      ctx.strokeStyle = 'rgba(255,255,255,0.12)';
      ctx.lineWidth = 1;
      ctx.stroke();
    }

    // Mini-sector colouring for delta channel
    if (name === 'delta' && data.mini_sectors.length > 0) {
      for (const ms of data.mini_sectors) {
        if (ms.distance_end < dStart || ms.distance_start > dEnd) continue;
        const sx = Math.max(toPixX(ms.distance_start), x0);
        const ex = Math.min(toPixX(ms.distance_end), x0 + plotW);
        ctx.fillStyle = ms.delta_s < 0
          ? `rgba(39,244,210,0.08)` // B faster
          : `rgba(255,128,0,0.08)`;   // A faster
        ctx.fillRect(sx, y0, ex - sx, h);
      }
    }

    // Draw lines
    const drawLine = (arr: number[], color: string) => {
      if (!arr || arr.length === 0) return;
      ctx.beginPath();
      let started = false;
      for (let i = 0; i < n; i++) {
        const d = data!.distances[i];
        if (d < dStart || d > dEnd) continue;
        const px = toPixX(d);
        const py = toPixY(arr[i]);
        if (!started) { ctx.moveTo(px, py); started = true; }
        else           { ctx.lineTo(px, py); }
      }
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.5;
      ctx.lineJoin = 'round';
      ctx.stroke();
    };

    // For throttle/brake, fill area
    if (name === 'throttle' || name === 'brake') {
      const fillArea = (arr: number[], color: string) => {
        if (!arr || arr.length === 0) return;
        ctx.beginPath();
        let first = true;
        for (let i = 0; i < n; i++) {
          const d = data!.distances[i];
          if (d < dStart || d > dEnd) continue;
          const px = toPixX(d);
          const py = toPixY(arr[i]);
          if (first) { ctx.moveTo(px, y0 + h - 1); ctx.lineTo(px, py); first = false; }
          else ctx.lineTo(px, py);
        }
        ctx.lineTo(toPixX(Math.min(data!.distances[n-1], dEnd)), y0 + h - 1);
        ctx.closePath();
        ctx.fillStyle = color + '44';
        ctx.fill();
      };
      if (yA) fillArea(yA, colorA);
      if (yB) fillArea(yB, colorB);
    }

    if (yA) drawLine(yA, name === 'delta' ? '#888888' : colorA);
    if (yB) drawLine(yB!, colorB);

    // For delta, draw filled area above/below zero
    if (name === 'delta') {
      const zy = toPixY(0);
      ctx.beginPath();
      let prevX = x0;
      let prevY = zy;
      for (let i = 0; i < n; i++) {
        const d = data!.distances[i];
        if (d < dStart || d > dEnd) continue;
        const px = toPixX(d);
        const py = toPixY(data!.delta_time[i]);
        if (i === 0 || data!.distances[i-1] < dStart) {
          ctx.moveTo(px, zy);
          ctx.lineTo(px, py);
        } else {
          ctx.lineTo(px, py);
        }
        prevX = px; prevY = py;
      }
      ctx.lineTo(prevX, zy);
      ctx.closePath();
      ctx.fillStyle = 'rgba(128,128,128,0.15)';
      ctx.fill();

      // Replot clean line on top
      drawLine(data.delta_time, 'rgba(255,255,255,0.7)');
    }
  }

  function drawDistanceAxis(yBase: number, plotW: number) {
    if (!data || data.distances.length === 0) return;
    const x0 = PADDING_LEFT;
    const dMax = data.distances[data.distances.length - 1];
    const dStart = viewStart * dMax;
    const dEnd   = viewEnd   * dMax;
    const viewRange = dEnd - dStart;

    // Choose tick interval
    const approxTicks = 8;
    const rawInterval = viewRange / approxTicks;
    const magnitude = Math.pow(10, Math.floor(Math.log10(rawInterval)));
    const niceIntervals = [1, 2, 5, 10];
    let interval = magnitude;
    for (const n of niceIntervals) {
      if (n * magnitude >= rawInterval) { interval = n * magnitude; break; }
    }

    ctx.fillStyle = 'rgba(255,255,255,0.25)';
    ctx.font = '8px monospace';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';

    const firstTick = Math.ceil(dStart / interval) * interval;
    for (let d = firstTick; d <= dEnd; d += interval) {
      const px = x0 + ((d - dStart) / viewRange) * plotW;
      ctx.beginPath();
      ctx.moveTo(px, yBase - 2);
      ctx.lineTo(px, yBase);
      ctx.strokeStyle = 'rgba(255,255,255,0.15)';
      ctx.lineWidth = 1;
      ctx.stroke();
      const label = d >= 1000 ? `${(d/1000).toFixed(1)}k` : `${Math.round(d)}`;
      ctx.fillText(label, px, yBase + 1);
    }
  }

  // ── Mouse interaction ──────────────────────────────────────────────────────

  function onMouseMove(e: MouseEvent) {
    const rect = canvas.getBoundingClientRect();
    const mx = (e.clientX - rect.left) * (canvas.width / rect.width);
    cursorX = mx;

    if (isDragging) {
      const dx = mx - dragStartX;
      const plotW = canvas.width - PADDING_LEFT - PADDING_RIGHT;
      const viewRange = dragStartView.end - dragStartView.start;
      const shift = -(dx / plotW) * viewRange;
      let ns = dragStartView.start + shift;
      let ne = dragStartView.end + shift;
      if (ns < 0) { ne -= ns; ns = 0; }
      if (ne > 1) { ns -= (ne - 1); ne = 1; }
      viewStart = Math.max(0, ns);
      viewEnd   = Math.min(1, ne);
    }
    draw();
  }

  function onMouseDown(e: MouseEvent) {
    const rect = canvas.getBoundingClientRect();
    isDragging = true;
    dragStartX = (e.clientX - rect.left) * (canvas.width / rect.width);
    dragStartView = { start: viewStart, end: viewEnd };
  }

  function onMouseUp() { isDragging = false; }
  function onMouseLeave() { isDragging = false; cursorX = null; draw(); }

  function onWheel(e: WheelEvent) {
    e.preventDefault();
    const rect = canvas.getBoundingClientRect();
    const mx = (e.clientX - rect.left) / rect.width;
    const frac = PADDING_LEFT / canvas.width + (mx * (1 - PADDING_LEFT/canvas.width));
    const pivot = viewStart + (viewEnd - viewStart) * frac;
    const factor = e.deltaY < 0 ? 0.85 : 1.15;
    let ns = pivot - (pivot - viewStart) * factor;
    let ne = pivot + (viewEnd - pivot) * factor;
    ns = Math.max(0, ns);
    ne = Math.min(1, ne);
    if (ne - ns > 0.005) { viewStart = ns; viewEnd = ne; }
    draw();
  }

  function resetZoom() {
    viewStart = 0; viewEnd = 1;
    draw();
  }
</script>

<div class="chart-wrap" bind:this={containerEl}>
  <canvas
    bind:this={canvas}
    on:mousemove={onMouseMove}
    on:mousedown={onMouseDown}
    on:mouseup={onMouseUp}
    on:mouseleave={onMouseLeave}
    on:wheel={onWheel}
    style="cursor: {isDragging ? 'grabbing' : 'crosshair'}"
  ></canvas>
  {#if data && (viewStart > 0.001 || viewEnd < 0.999)}
    <button class="reset-btn" on:click={resetZoom}>RESET</button>
  {/if}
</div>

<style>
  .chart-wrap {
    position: relative;
    width: 100%;
    height: 100%;
  }
  canvas {
    display: block;
    width: 100%;
    height: 100%;
  }
  .reset-btn {
    position: absolute;
    top: 4px;
    right: 8px;
    padding: 2px 6px;
    background: rgba(255,255,255,0.08);
    border: 1px solid rgba(255,255,255,0.15);
    color: rgba(255,255,255,0.5);
    font-size: 8px;
    font-family: inherit;
    font-weight: 700;
    letter-spacing: 0.1em;
    cursor: pointer;
    border-radius: 2px;
  }
</style>
