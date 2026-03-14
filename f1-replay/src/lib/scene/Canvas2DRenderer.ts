import type { DriverFrame, HeatCell } from '$lib/commands';
import { TEAM_COLORS_HEX } from '$lib/constants';

// ── Speed colour ramp (matches design spec) ──────────────────────────────────
const SPEED_RAMP: [number, string][] = [
  [0.00, '#1a2a6c'],
  [0.25, '#1a5cff'],
  [0.50, '#00e5cc'],
  [0.75, '#ffe100'],
  [1.00, '#ff2200'],
];

function speedColor(norm: number): string {
  for (let i = 0; i < SPEED_RAMP.length - 1; i++) {
    const [t0, c0] = SPEED_RAMP[i];
    const [t1, c1] = SPEED_RAMP[i + 1];
    if (norm <= t1) {
      const f = (norm - t0) / (t1 - t0);
      return lerpColor(c0, c1, f);
    }
  }
  return SPEED_RAMP[SPEED_RAMP.length - 1][1];
}

function lerpColor(a: string, b: string, t: number): string {
  const ar = parseInt(a.slice(1, 3), 16);
  const ag = parseInt(a.slice(3, 5), 16);
  const ab = parseInt(a.slice(5, 7), 16);
  const br = parseInt(b.slice(1, 3), 16);
  const bg = parseInt(b.slice(3, 5), 16);
  const bb = parseInt(b.slice(5, 7), 16);
  const r = Math.round(ar + (br - ar) * t);
  const g = Math.round(ag + (bg - ag) * t);
  const bv = Math.round(ab + (bb - ab) * t);
  return `#${r.toString(16).padStart(2,'0')}${g.toString(16).padStart(2,'0')}${bv.toString(16).padStart(2,'0')}`;
}

// ── Trail storage ─────────────────────────────────────────────────────────────
const TRAIL_LEN = 60;

interface CarState {
  x: number;
  y: number;
  heading: number;
  speed: number;
  brake: number;
  drs_active: boolean;
  is_in_pit: boolean;
  position: number;
  compound: string;
  trail: Array<{ x: number; y: number }>;
}

// ── Canvas2DRenderer ──────────────────────────────────────────────────────────

export class Canvas2DRenderer {
  private canvas!: HTMLCanvasElement;
  private ctx!: CanvasRenderingContext2D;

  // track coordinate space
  private trackCX = 0;
  private trackCY = 0;
  private baseScale = 1; // pixels per track unit at zoom=1

  // view state
  private zoom = 1;
  private panX = 0;
  private panY = 0;

  // data
  private centerLine: [number, number][] = [];
  private heatCells: HeatCell[] = [];
  private carStates: Map<string, CarState> = new Map();
  private abbrMap: Map<string, string> = new Map();
  private teamMap: Map<string, string> = new Map();
  private focusedDriver: string | null = null;

  // mouse
  private isDragging = false;
  private lastMouse = { x: 0, y: 0 };

  // pre-built heatmap image cache
  private heatImageData: ImageData | null = null;
  private heatCacheScale = 0;

  init(canvas: HTMLCanvasElement) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d')!;
    this.setupEvents();
  }

  setTrackBounds(xMin: number, xMax: number, yMin: number, yMax: number) {
    this.trackCX = (xMin + xMax) / 2;
    this.trackCY = (yMin + yMax) / 2;
    const trackW = xMax - xMin;
    const trackH = yMax - yMin;
    const cw = this.canvas.width;
    const ch = this.canvas.height;
    const scaleByW = (cw * 0.85) / trackW;
    const scaleByH = (ch * 0.85) / trackH;
    this.baseScale = Math.min(scaleByW, scaleByH);
    this.zoom = 1;
    this.panX = 0;
    this.panY = 0;
    this.heatImageData = null;
    this.heatCacheScale = 0;
  }

  resize(width: number, height: number) {
    // Recompute base scale if we have bounds
    if (this.baseScale > 0 && this.canvas.width > 0) {
      const oldCW = this.canvas.width;
      const ratio = width / oldCW;
      this.baseScale *= ratio;
    }
    this.heatImageData = null;
  }

  setCenterLine(line: [number, number][]) {
    this.centerLine = line;
  }

  setHeatmap(cells: HeatCell[]) {
    this.heatCells = cells;
    this.heatImageData = null;
    this.heatCacheScale = 0;
  }

  setupDrivers(
    driverNumbers: string[],
    abbrMap: Record<string, string>,
    teamMap: Record<string, string>
  ) {
    this.carStates.clear();
    this.abbrMap = new Map(Object.entries(abbrMap));
    this.teamMap = new Map(Object.entries(teamMap));
    for (const num of driverNumbers) {
      this.carStates.set(num, {
        x: 0, y: 0, heading: 0, speed: 0, brake: 0,
        drs_active: false, is_in_pit: false, position: 20, compound: 'HARD',
        trail: [],
      });
    }
  }

  update(frames: DriverFrame[]) {
    for (const f of frames) {
      const state = this.carStates.get(f.driver_number);
      if (!state) continue;
      // Append to trail before updating position
      if (state.x !== 0 || state.y !== 0) {
        state.trail.push({ x: state.x, y: state.y });
        if (state.trail.length > TRAIL_LEN) state.trail.shift();
      }
      state.x = f.x;
      state.y = f.y;
      state.heading = f.heading;
      state.speed = f.speed;
      state.brake = f.brake;
      state.drs_active = f.drs_active;
      state.is_in_pit = f.is_in_pit;
      state.position = f.position;
      state.compound = f.compound;
    }
  }

  setFocus(driverNumber: string | null) {
    this.focusedDriver = driverNumber;
    if (driverNumber) {
      const state = this.carStates.get(driverNumber);
      if (state && (state.x !== 0 || state.y !== 0)) {
        // Center view on this driver
        this.panX = 0;
        this.panY = 0;
      }
    }
  }

  render() {
    const ctx = this.ctx;
    const cw = this.canvas.width;
    const ch = this.canvas.height;

    ctx.clearRect(0, 0, cw, ch);
    ctx.fillStyle = '#0f0f10';
    ctx.fillRect(0, 0, cw, ch);

    if (this.centerLine.length === 0) return;

    // Build heatmap image if needed
    const effectiveScale = this.baseScale * this.zoom;
    if (!this.heatImageData || Math.abs(effectiveScale - this.heatCacheScale) / effectiveScale > 0.05) {
      this.buildHeatImage(cw, ch);
      this.heatCacheScale = effectiveScale;
    }

    if (this.heatImageData) {
      ctx.putImageData(this.heatImageData, 0, 0);
    }

    this.drawTrack(ctx, cw, ch);
    this.drawTrails(ctx);
    this.drawCars(ctx);
  }

  // ── Coordinate transforms ──────────────────────────────────────────────────

  private tx(trackX: number): number {
    const cw = this.canvas.width;
    return (trackX - this.trackCX) * this.baseScale * this.zoom + cw / 2 + this.panX;
  }

  private ty(trackY: number): number {
    const ch = this.canvas.height;
    // Y is inverted: track Y up = canvas Y up
    return -(trackY - this.trackCY) * this.baseScale * this.zoom + ch / 2 + this.panY;
  }

  // ── Heatmap as bitmap ──────────────────────────────────────────────────────

  private buildHeatImage(cw: number, ch: number) {
    if (this.heatCells.length === 0) return;

    const imgData = this.ctx.createImageData(cw, ch);
    const data = imgData.data;
    const scale = this.baseScale * this.zoom;
    const radius = Math.max(3, Math.round(scale * 60)); // ~60 track units per cell

    for (const cell of this.heatCells) {
      const px = Math.round(this.tx(cell.x));
      const py = Math.round(this.ty(cell.y));
      const col = hexToRgb(speedColor(cell.speed_norm));
      if (!col) continue;

      const r2 = radius * radius;
      const x0 = Math.max(0, px - radius);
      const x1 = Math.min(cw - 1, px + radius);
      const y0 = Math.max(0, py - radius);
      const y1 = Math.min(ch - 1, py + radius);

      for (let y = y0; y <= y1; y++) {
        for (let x = x0; x <= x1; x++) {
          const dx = x - px;
          const dy = y - py;
          const d2 = dx * dx + dy * dy;
          if (d2 > r2) continue;
          const alpha = (1 - Math.sqrt(d2) / radius) * 200;
          const i = (y * cw + x) * 4;
          // Blend: max alpha wins
          if (alpha > data[i + 3]) {
            data[i]     = col.r;
            data[i + 1] = col.g;
            data[i + 2] = col.b;
            data[i + 3] = alpha;
          }
        }
      }
    }

    this.heatImageData = imgData;
  }

  // ── Track centerline ───────────────────────────────────────────────────────

  private drawTrack(ctx: CanvasRenderingContext2D, _cw: number, _ch: number) {
    if (this.centerLine.length < 2) return;

    // Outer shadow / base track width
    ctx.beginPath();
    ctx.moveTo(this.tx(this.centerLine[0][0]), this.ty(this.centerLine[0][1]));
    for (let i = 1; i < this.centerLine.length; i++) {
      ctx.lineTo(this.tx(this.centerLine[i][0]), this.ty(this.centerLine[i][1]));
    }
    // Close loop if first/last are close
    const first = this.centerLine[0];
    const last = this.centerLine[this.centerLine.length - 1];
    const dx = first[0] - last[0];
    const dy = first[1] - last[1];
    if (Math.sqrt(dx * dx + dy * dy) < 500) ctx.closePath();

    const scale = this.baseScale * this.zoom;
    const trackWidth = Math.max(3, scale * 15); // 15 track units ≈ road width

    ctx.lineWidth = trackWidth + 4;
    ctx.strokeStyle = 'rgba(255,255,255,0.05)';
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';
    ctx.stroke();

    ctx.lineWidth = trackWidth;
    ctx.strokeStyle = 'rgba(40,40,45,0.9)';
    ctx.stroke();

    // White edge line
    ctx.lineWidth = 1.5;
    ctx.strokeStyle = 'rgba(255,255,255,0.12)';
    ctx.stroke();
  }

  // ── Trails ─────────────────────────────────────────────────────────────────

  private drawTrails(ctx: CanvasRenderingContext2D) {
    for (const [num, state] of this.carStates) {
      if (state.trail.length < 2) continue;
      const team = this.teamMap.get(num) ?? 'Unknown';
      const color = TEAM_COLORS_HEX[team] ?? '#888';
      ctx.beginPath();
      ctx.moveTo(this.tx(state.trail[0].x), this.ty(state.trail[0].y));
      for (let i = 1; i < state.trail.length; i++) {
        ctx.lineTo(this.tx(state.trail[i].x), this.ty(state.trail[i].y));
      }
      ctx.lineWidth = 1.5;
      ctx.strokeStyle = color + '55'; // 33% alpha
      ctx.lineCap = 'round';
      ctx.stroke();
    }
  }

  // ── Cars ───────────────────────────────────────────────────────────────────

  private drawCars(ctx: CanvasRenderingContext2D) {
    const scale = this.baseScale * this.zoom;
    const dotR = Math.max(4, Math.min(12, scale * 8));
    const fontSize = Math.max(8, Math.min(14, dotR * 1.1));

    // Draw in reverse position order so P1 is on top
    const sorted = [...this.carStates.entries()].sort(
      ([, a], [, b]) => b.position - a.position
    );

    for (const [num, state] of sorted) {
      if (state.x === 0 && state.y === 0) continue;
      const cx = this.tx(state.x);
      const cy = this.ty(state.y);
      const team = this.teamMap.get(num) ?? 'Unknown';
      const color = TEAM_COLORS_HEX[team] ?? '#888888';
      const abbr = this.abbrMap.get(num) ?? num;
      const isFocused = this.focusedDriver === num;

      // Brake glow (subtle red overlay, no bloom)
      if (state.brake > 0.3) {
        ctx.beginPath();
        ctx.arc(cx, cy, dotR * 1.8, 0, Math.PI * 2);
        ctx.fillStyle = `rgba(220,30,0,${state.brake * 0.25})`;
        ctx.fill();
      }

      // DRS ring
      if (state.drs_active) {
        ctx.beginPath();
        ctx.arc(cx, cy, dotR * 1.5, 0, Math.PI * 2);
        ctx.strokeStyle = 'rgba(0,230,180,0.5)';
        ctx.lineWidth = 1.5;
        ctx.stroke();
      }

      // Focus ring
      if (isFocused) {
        ctx.beginPath();
        ctx.arc(cx, cy, dotR * 1.7, 0, Math.PI * 2);
        ctx.strokeStyle = 'rgba(255,255,255,0.8)';
        ctx.lineWidth = 2;
        ctx.stroke();
      }

      // Car dot
      ctx.beginPath();
      ctx.arc(cx, cy, dotR, 0, Math.PI * 2);
      ctx.fillStyle = color;
      ctx.fill();
      ctx.strokeStyle = 'rgba(255,255,255,0.6)';
      ctx.lineWidth = 1;
      ctx.stroke();

      // Abbreviation label (only if zoom > 0.5)
      if (this.zoom > 0.5 || isFocused) {
        ctx.font = `bold ${fontSize}px "JetBrains Mono", monospace`;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        // Shadow for legibility
        ctx.fillStyle = 'rgba(0,0,0,0.8)';
        ctx.fillText(abbr, cx + 1, cy + dotR + fontSize + 1);
        ctx.fillStyle = '#ffffff';
        ctx.fillText(abbr, cx, cy + dotR + fontSize);
      }
    }
  }

  // ── Mouse controls ─────────────────────────────────────────────────────────

  private setupEvents() {
    const canvas = this.canvas;

    canvas.addEventListener('mousedown', (e) => {
      this.isDragging = true;
      this.lastMouse = { x: e.clientX, y: e.clientY };
    });

    canvas.addEventListener('mousemove', (e) => {
      if (!this.isDragging) return;
      const dx = e.clientX - this.lastMouse.x;
      const dy = e.clientY - this.lastMouse.y;
      this.panX += dx;
      this.panY += dy;
      this.lastMouse = { x: e.clientX, y: e.clientY };
      this.heatImageData = null;
    });

    canvas.addEventListener('mouseup', () => { this.isDragging = false; });
    canvas.addEventListener('mouseleave', () => { this.isDragging = false; });

    canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      const factor = e.deltaY < 0 ? 1.1 : 0.9;
      // Zoom toward cursor
      const rect = canvas.getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;
      const cx = canvas.width / 2;
      const cy = canvas.height / 2;
      this.panX = mx - (mx - this.panX) * factor - (cx - cx * factor);
      this.panY = my - (my - this.panY) * factor - (cy - cy * factor);
      this.zoom = Math.max(0.2, Math.min(15, this.zoom * factor));
      this.heatImageData = null;
    }, { passive: false });
  }
}

// ── Utility ───────────────────────────────────────────────────────────────────

function hexToRgb(hex: string): { r: number; g: number; b: number } | null {
  const m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex);
  if (!m) return null;
  return { r: parseInt(m[1], 16), g: parseInt(m[2], 16), b: parseInt(m[3], 16) };
}
