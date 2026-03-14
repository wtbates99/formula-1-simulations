import * as THREE from 'three';
import type { DriverFrame } from '$lib/commands';
import type { SceneManager } from './SceneManager';
import { TEAM_COLORS } from '$lib/constants';

const TRAIL_LENGTH = 80;

// ── Car shaders ───────────────────────────────────────────────────────────────

const CAR_VERT = /* glsl */`
uniform vec3 teamColor;
uniform float brakeIntensity;
uniform float drsActive;
varying vec2 vUv;
void main() {
  vUv = uv;
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}`;

const CAR_FRAG = /* glsl */`
uniform vec3 teamColor;
uniform float brakeIntensity;
uniform float drsActive;
varying vec2 vUv;
void main() {
  vec2 c = vUv - 0.5;
  float dist = length(c);
  if (dist > 0.5) discard;

  float core = 1.0 - smoothstep(0.0, 0.3, dist);
  float glow = 1.0 - smoothstep(0.2, 0.5, dist);

  vec3 baseColor = teamColor;
  baseColor = mix(baseColor, vec3(1.0, 0.1, 0.0), brakeIntensity * 0.8);
  vec3 drsColor = mix(baseColor, vec3(0.0, 0.8, 1.0), drsActive * 0.5);

  vec3 finalColor = drsColor * (core * 3.0 + glow * 1.5);
  float alpha = glow * 0.95;

  gl_FragColor = vec4(finalColor, alpha);
}`;

// ── Types ─────────────────────────────────────────────────────────────────────

interface CarObjects {
  body: THREE.Mesh;
  drsRing: THREE.Mesh;
  trailPositions: Float32Array;
  trailHead: number;
  trailGeo: THREE.BufferGeometry;
  trailLine: THREE.Line;
  teamColor: THREE.Color;
}

// ── CarRenderer ───────────────────────────────────────────────────────────────

export class CarRenderer {
  private cars: Map<string, CarObjects> = new Map();
  private scene!: THREE.Scene;
  private sm!: SceneManager;

  init(scene: THREE.Scene, sm: SceneManager) {
    this.scene = scene;
    this.sm = sm;
  }

  setupDrivers(driverNumbers: string[], teamMap: Record<string, string>) {
    // Dispose and remove existing cars
    for (const [, objs] of this.cars) {
      this.scene.remove(objs.body, objs.drsRing, objs.trailLine);
      objs.body.geometry.dispose();
      (objs.body.material as THREE.Material).dispose();
      objs.drsRing.geometry.dispose();
      (objs.drsRing.material as THREE.Material).dispose();
      objs.trailGeo.dispose();
    }
    this.cars.clear();

    for (const num of driverNumbers) {
      const team = teamMap[num] ?? 'Unknown';
      const teamHex = TEAM_COLORS[team] ?? 0xffffff;
      const teamColor = new THREE.Color(teamHex);

      // ── Car body ──────────────────────────────────────────────────────────
      const bodyGeo = new THREE.PlaneGeometry(120, 120);
      const bodyMat = new THREE.ShaderMaterial({
        vertexShader: CAR_VERT,
        fragmentShader: CAR_FRAG,
        uniforms: {
          teamColor:     { value: teamColor.clone() },
          brakeIntensity: { value: 0 },
          drsActive:     { value: 0 },
        },
        transparent: true,
        depthWrite: false,
        side: THREE.DoubleSide,
      });
      const body = new THREE.Mesh(bodyGeo, bodyMat);
      body.position.z = 2;
      body.renderOrder = 2;
      this.scene.add(body);

      // ── DRS ring ──────────────────────────────────────────────────────────
      const ringGeo = new THREE.RingGeometry(70, 90, 32);
      const ringMat = new THREE.MeshBasicMaterial({
        color: 0x00ccff,
        transparent: true,
        opacity: 0,
        side: THREE.DoubleSide,
        depthWrite: false,
      });
      const drsRing = new THREE.Mesh(ringGeo, ringMat);
      drsRing.position.z = 1;
      drsRing.renderOrder = 1;
      this.scene.add(drsRing);

      // ── Trail ─────────────────────────────────────────────────────────────
      const trailPositions = new Float32Array(TRAIL_LENGTH * 3).fill(0);
      const trailColors = new Float32Array(TRAIL_LENGTH * 3);
      for (let i = 0; i < TRAIL_LENGTH; i++) {
        const f = i / TRAIL_LENGTH;
        trailColors[i * 3]     = teamColor.r * f;
        trailColors[i * 3 + 1] = teamColor.g * f;
        trailColors[i * 3 + 2] = teamColor.b * f;
      }

      const trailGeo = new THREE.BufferGeometry();
      trailGeo.setAttribute('position', new THREE.BufferAttribute(trailPositions.slice(), 3));
      trailGeo.setAttribute('color',    new THREE.BufferAttribute(trailColors, 3));

      const trailMat = new THREE.LineBasicMaterial({
        vertexColors: true,
        transparent: true,
        opacity: 0.7,
        depthWrite: false,
      });
      const trailLine = new THREE.Line(trailGeo, trailMat);
      trailLine.renderOrder = 0;
      trailLine.frustumCulled = false;
      this.scene.add(trailLine);

      this.cars.set(num, {
        body,
        drsRing,
        trailPositions,
        trailHead: 0,
        trailGeo,
        trailLine,
        teamColor: teamColor.clone(),
      });
    }
  }

  update(frames: DriverFrame[]) {
    for (const f of frames) {
      const car = this.cars.get(f.driver_number);
      if (!car) continue;

      const wx = this.sm.worldX(f.x);
      const wy = this.sm.worldY(f.y);

      // ── Position & rotation ───────────────────────────────────────────────
      car.body.position.set(wx, wy, 2);
      car.body.rotation.z = f.heading;

      // ── Shader uniforms ───────────────────────────────────────────────────
      const mat = car.body.material as THREE.ShaderMaterial;
      mat.uniforms.brakeIntensity.value = f.brake;
      mat.uniforms.drsActive.value = f.drs_active ? 1.0 : 0.0;

      // ── DRS ring ──────────────────────────────────────────────────────────
      car.drsRing.position.set(wx, wy, 1);
      (car.drsRing.material as THREE.MeshBasicMaterial).opacity = f.drs_active ? 0.6 : 0.0;

      // ── Trail ring-buffer update ──────────────────────────────────────────
      const head = car.trailHead;
      car.trailPositions[head * 3]     = wx;
      car.trailPositions[head * 3 + 1] = wy;
      car.trailPositions[head * 3 + 2] = 0;
      car.trailHead = (head + 1) % TRAIL_LENGTH;

      // Rebuild ordered trail from oldest to newest
      const posAttr = car.trailGeo.getAttribute('position') as THREE.BufferAttribute;
      const posArr = posAttr.array as Float32Array;
      for (let i = 0; i < TRAIL_LENGTH; i++) {
        const srcIdx = (car.trailHead + i) % TRAIL_LENGTH;
        posArr[i * 3]     = car.trailPositions[srcIdx * 3];
        posArr[i * 3 + 1] = car.trailPositions[srcIdx * 3 + 1];
        posArr[i * 3 + 2] = car.trailPositions[srcIdx * 3 + 2];
      }
      posAttr.needsUpdate = true;

      // Update trail fade colours
      const colAttr = car.trailGeo.getAttribute('color') as THREE.BufferAttribute;
      const colArr = colAttr.array as Float32Array;
      for (let i = 0; i < TRAIL_LENGTH; i++) {
        const intensity = (i / TRAIL_LENGTH) * (i / TRAIL_LENGTH);
        colArr[i * 3]     = car.teamColor.r * intensity;
        colArr[i * 3 + 1] = car.teamColor.g * intensity;
        colArr[i * 3 + 2] = car.teamColor.b * intensity;
      }
      colAttr.needsUpdate = true;
    }
  }

  focusOnDriver(driverNumber: string | null, sm: SceneManager) {
    if (!driverNumber) return;
    const car = this.cars.get(driverNumber);
    if (!car) return;
    // Access private fields via index signature — acceptable for internal tooling
    (sm as unknown as Record<string, unknown>)['targetCamX'] = car.body.position.x;
    (sm as unknown as Record<string, unknown>)['targetCamY'] = car.body.position.y;
    (sm as unknown as { updateCamera(): void }).updateCamera?.();
  }
}
