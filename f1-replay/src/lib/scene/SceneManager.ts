import * as THREE from 'three';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';

export class SceneManager {
  renderer!: THREE.WebGLRenderer;
  scene!: THREE.Scene;
  camera!: THREE.OrthographicCamera;
  composer!: EffectComposer;
  bloomPass!: UnrealBloomPass;

  private width = 1;
  private height = 1;

  // Track origin (world coords are relative to track center)
  private trackCX = 0;
  private trackCY = 0;
  // View height in track units – set from actual track bounds
  private baseViewH = 22000;

  // Camera pan/zoom state
  private isDragging = false;
  private lastMouse = { x: 0, y: 0 };
  private targetCamX = 0;
  private targetCamY = 0;
  private zoom = 1;

  init(canvas: HTMLCanvasElement, containerW?: number, containerH?: number) {
    this.width  = containerW ?? (canvas.clientWidth  || 1200);
    this.height = containerH ?? (canvas.clientHeight || 800);

    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.setSize(this.width, this.height);
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.0;
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;

    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x03010a);

    this.camera = this.makeOrthoCamera();

    this.composer = new EffectComposer(this.renderer);
    this.composer.addPass(new RenderPass(this.scene, this.camera));

    this.bloomPass = new UnrealBloomPass(
      new THREE.Vector2(this.width, this.height),
      1.2,  // strength
      0.5,  // radius
      0.15  // threshold
    );
    this.composer.addPass(this.bloomPass);
    this.composer.addPass(new OutputPass());

    this.setupMouseControls(canvas);
  }

  private makeOrthoCamera(): THREE.OrthographicCamera {
    const aspect = this.width / this.height;
    const viewH = this.baseViewH / this.zoom;
    const viewW = viewH * aspect;
    const cam = new THREE.OrthographicCamera(
      -viewW / 2, viewW / 2, viewH / 2, -viewH / 2,
      0.1, 200000
    );
    cam.position.set(this.targetCamX, this.targetCamY, 100000);
    cam.lookAt(this.targetCamX, this.targetCamY, 0);
    return cam;
  }

  setTrackBounds(xMin: number, xMax: number, yMin: number, yMax: number) {
    this.trackCX = (xMin + xMax) / 2;
    this.trackCY = (yMin + yMax) / 2;
    this.targetCamX = 0;
    this.targetCamY = 0;
    // Fit the full track into view with 15% padding on each side
    const trackW = xMax - xMin;
    const trackH = yMax - yMin;
    const aspect = this.width / this.height;
    // Choose the dimension that limits the view
    const fitByH = trackH * 1.3;
    const fitByW = (trackW * 1.3) / aspect;
    this.baseViewH = Math.max(fitByH, fitByW, 1000);
    this.zoom = 1;
    this.updateCamera();
  }

  private updateCamera() {
    const aspect = this.width / this.height;
    const viewH = this.baseViewH / this.zoom;
    const viewW = viewH * aspect;
    this.camera.left   = -viewW / 2 + this.targetCamX;
    this.camera.right  =  viewW / 2 + this.targetCamX;
    this.camera.top    =  viewH / 2 + this.targetCamY;
    this.camera.bottom = -viewH / 2 + this.targetCamY;
    this.camera.updateProjectionMatrix();
  }

  private setupMouseControls(canvas: HTMLCanvasElement) {
    canvas.addEventListener('mousedown', (e) => {
      this.isDragging = true;
      this.lastMouse = { x: e.clientX, y: e.clientY };
    });

    canvas.addEventListener('mousemove', (e) => {
      if (!this.isDragging) return;
      const dx = e.clientX - this.lastMouse.x;
      const dy = e.clientY - this.lastMouse.y;
      const scale = (this.baseViewH / this.zoom) / this.height;
      this.targetCamX -= dx * scale;
      this.targetCamY += dy * scale;
      this.lastMouse = { x: e.clientX, y: e.clientY };
      this.updateCamera();
    });

    canvas.addEventListener('mouseup', () => { this.isDragging = false; });
    canvas.addEventListener('mouseleave', () => { this.isDragging = false; });

    canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.zoom *= e.deltaY < 0 ? 1.1 : 0.9;
      this.zoom = Math.max(0.3, Math.min(10, this.zoom));
      this.updateCamera();
    }, { passive: false });
  }

  resize(width: number, height: number) {
    this.width = width;
    this.height = height;
    this.renderer.setSize(width, height);
    this.composer.setSize(width, height);
    this.bloomPass.setSize(width, height);
    this.updateCamera();
  }

  /** Convert track X coordinate to world (scene) X. */
  worldX(trackX: number): number {
    return trackX - this.trackCX;
  }

  /** Convert track Y coordinate to world (scene) Y. */
  worldY(trackY: number): number {
    return trackY - this.trackCY;
  }

  render() {
    this.composer.render();
  }
}
