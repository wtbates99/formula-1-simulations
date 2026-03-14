import * as THREE from 'three';
import type { HeatCell } from '$lib/commands';
import type { SceneManager } from './SceneManager';

// ── Heatmap shaders ───────────────────────────────────────────────────────────

const HEATMAP_VERT = /* glsl */`
attribute float pointSize;
varying vec3 vColor;
void main() {
  vColor = color;
  vec4 mvPos = modelViewMatrix * vec4(position, 1.0);
  gl_PointSize = pointSize;
  gl_Position = projectionMatrix * mvPos;
}`;

const HEATMAP_FRAG = /* glsl */`
varying vec3 vColor;
void main() {
  vec2 c = gl_PointCoord - 0.5;
  float r2 = dot(c, c);
  if (r2 > 0.25) discard;
  float alpha = (1.0 - smoothstep(0.15, 0.25, r2)) * 0.92;
  gl_FragColor = vec4(vColor, alpha);
}`;

// ── Colour gradient ───────────────────────────────────────────────────────────

type Stop = [number, number];

const SPEED_STOPS: Stop[] = [
  [0.00, 0x0d0221],
  [0.25, 0x0044aa],
  [0.50, 0x00bbcc],
  [0.75, 0xffcc00],
  [1.00, 0xff1100],
];

function speedToColor(norm: number): THREE.Color {
  for (let i = 0; i < SPEED_STOPS.length - 1; i++) {
    const [t0, c0] = SPEED_STOPS[i];
    const [t1, c1] = SPEED_STOPS[i + 1];
    if (norm <= t1) {
      const f = (norm - t0) / (t1 - t0);
      return new THREE.Color(c0).lerp(new THREE.Color(c1), f);
    }
  }
  return new THREE.Color(0xff1100);
}

// ── TrackRenderer ─────────────────────────────────────────────────────────────

export class TrackRenderer {
  private heatmapPoints?: THREE.Points;
  private trackLine?: THREE.Line;

  buildHeatmap(cells: HeatCell[], scene: THREE.Scene, sm: SceneManager) {
    // Remove old heatmap if exists
    if (this.heatmapPoints) {
      scene.remove(this.heatmapPoints);
      (this.heatmapPoints.geometry as THREE.BufferGeometry).dispose();
      this.heatmapPoints = undefined;
    }

    const n = cells.length;
    if (n === 0) return;

    const positions = new Float32Array(n * 3);
    const colors    = new Float32Array(n * 3);
    const sizes     = new Float32Array(n);

    cells.forEach((cell, i) => {
      positions[i * 3]     = sm.worldX(cell.x);
      positions[i * 3 + 1] = sm.worldY(cell.y);
      positions[i * 3 + 2] = -2;

      const col = speedToColor(cell.speed_norm);
      colors[i * 3]     = col.r;
      colors[i * 3 + 1] = col.g;
      colors[i * 3 + 2] = col.b;

      sizes[i] = 48;
    });

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position',  new THREE.BufferAttribute(positions, 3));
    geo.setAttribute('color',     new THREE.BufferAttribute(colors, 3));
    geo.setAttribute('pointSize', new THREE.BufferAttribute(sizes, 1));

    const mat = new THREE.ShaderMaterial({
      vertexShader:   HEATMAP_VERT,
      fragmentShader: HEATMAP_FRAG,
      vertexColors:   true,
      transparent:    true,
      depthWrite:     false,
    });

    this.heatmapPoints = new THREE.Points(geo, mat);
    this.heatmapPoints.renderOrder = -1;
    scene.add(this.heatmapPoints);
  }

  buildTrackOutline(
    centerLine: [number, number][],
    scene: THREE.Scene,
    sm: SceneManager
  ) {
    if (this.trackLine) {
      scene.remove(this.trackLine);
      this.trackLine.geometry.dispose();
      this.trackLine = undefined;
    }

    if (centerLine.length === 0) return;

    const points = centerLine.map(([x, y]) =>
      new THREE.Vector3(sm.worldX(x), sm.worldY(y), -3)
    );

    const geo = new THREE.BufferGeometry().setFromPoints(points);
    const mat = new THREE.LineBasicMaterial({
      color: 0xffffff,
      opacity: 0.08,
      transparent: true,
    });

    this.trackLine = new THREE.Line(geo, mat);
    this.trackLine.renderOrder = -2;
    scene.add(this.trackLine);
  }
}
