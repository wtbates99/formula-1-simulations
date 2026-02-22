import type { TrackConfig } from './types';

type CarViews = { x: Float32Array; y: Float32Array; yaw: Float32Array; speed: Float32Array };

function createShader(gl: WebGL2RenderingContext, type: number, src: string): WebGLShader {
  const shader = gl.createShader(type);
  if (!shader) throw new Error('shader alloc failed');
  gl.shaderSource(shader, src);
  gl.compileShader(shader);
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    throw new Error(gl.getShaderInfoLog(shader) ?? 'shader compile failed');
  }
  return shader;
}

function createProgram(gl: WebGL2RenderingContext, vs: string, fs: string): WebGLProgram {
  const p = gl.createProgram();
  if (!p) throw new Error('program alloc failed');
  const v = createShader(gl, gl.VERTEX_SHADER, vs);
  const f = createShader(gl, gl.FRAGMENT_SHADER, fs);
  gl.attachShader(p, v);
  gl.attachShader(p, f);
  gl.linkProgram(p);
  gl.deleteShader(v);
  gl.deleteShader(f);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
    throw new Error(gl.getProgramInfoLog(p) ?? 'program link failed');
  }
  return p;
}

function curvatureAt(nodes: Array<[number, number, number]>, s: number, length: number): number {
  let sw = s;
  while (sw < 0) sw += length;
  while (sw >= length) sw -= length;

  for (let i = 1; i < nodes.length; i += 1) {
    if (sw <= nodes[i][0]) {
      const s0 = nodes[i - 1][0];
      const s1 = nodes[i][0];
      const t = (sw - s0) / (s1 - s0);
      return nodes[i - 1][1] + (nodes[i][1] - nodes[i - 1][1]) * t;
    }
  }

  const s0 = nodes[nodes.length - 1][0];
  const s1 = length + nodes[0][0];
  const t = (sw - s0) / (s1 - s0);
  return nodes[nodes.length - 1][1] + (nodes[0][1] - nodes[nodes.length - 1][1]) * t;
}

export function buildTrackPolyline(track: TrackConfig, samples = 2048): Float32Array {
  const out = new Float32Array(samples * 2);
  const ds = track.length_m / samples;

  let x = 0;
  let y = 0;
  let yaw = 0;

  for (let i = 0; i < samples; i += 1) {
    out[i * 2 + 0] = x;
    out[i * 2 + 1] = y;
    const s = i * ds;
    yaw += curvatureAt(track.nodes, s, track.length_m) * ds;
    x += Math.cos(yaw) * ds;
    y += Math.sin(yaw) * ds;
  }

  return out;
}

export class Renderer {
  private gl: WebGL2RenderingContext;
  private trackProgram: WebGLProgram;
  private carProgram: WebGLProgram;

  private trackBuffer: WebGLBuffer;
  private carBuffer: WebGLBuffer;

  private trackVerts = 0;
  private scale = 0.01;
  private offsetX = 0;
  private offsetY = 0;

  constructor(private canvas: HTMLCanvasElement) {
    const gl = canvas.getContext('webgl2', { antialias: true, alpha: false });
    if (!gl) throw new Error('WebGL2 unavailable');
    this.gl = gl;

    this.trackProgram = createProgram(
      gl,
      `#version 300 es
      layout(location=0) in vec2 aPos;
      uniform vec2 uScale;
      uniform vec2 uOffset;
      void main() {
        vec2 p = (aPos + uOffset) * uScale;
        gl_Position = vec4(p, 0.0, 1.0);
      }`,
      `#version 300 es
      precision highp float;
      out vec4 frag;
      void main() { frag = vec4(0.96, 0.26, 0.08, 1.0); }`
    );

    this.carProgram = createProgram(
      gl,
      `#version 300 es
      layout(location=0) in vec2 aPos;
      uniform vec2 uScale;
      uniform vec2 uOffset;
      void main() {
        vec2 p = (aPos + uOffset) * uScale;
        gl_Position = vec4(p, 0.0, 1.0);
        gl_PointSize = 6.0;
      }`,
      `#version 300 es
      precision highp float;
      out vec4 frag;
      void main() {
        vec2 c = gl_PointCoord - vec2(0.5);
        float d = dot(c, c);
        if (d > 0.25) discard;
        frag = vec4(0.12, 0.86, 0.74, 1.0);
      }`
    );

    const trackBuffer = gl.createBuffer();
    const carBuffer = gl.createBuffer();
    if (!trackBuffer || !carBuffer) throw new Error('buffer alloc failed');

    this.trackBuffer = trackBuffer;
    this.carBuffer = carBuffer;
  }

  setTrack(trackLine: Float32Array): void {
    const gl = this.gl;
    this.trackVerts = trackLine.length / 2;

    let minX = Number.POSITIVE_INFINITY;
    let minY = Number.POSITIVE_INFINITY;
    let maxX = Number.NEGATIVE_INFINITY;
    let maxY = Number.NEGATIVE_INFINITY;

    for (let i = 0; i < this.trackVerts; i += 1) {
      const x = trackLine[i * 2 + 0];
      const y = trackLine[i * 2 + 1];
      minX = Math.min(minX, x);
      minY = Math.min(minY, y);
      maxX = Math.max(maxX, x);
      maxY = Math.max(maxY, y);
    }

    const w = Math.max(1, maxX - minX);
    const h = Math.max(1, maxY - minY);
    this.offsetX = -(minX + maxX) * 0.5;
    this.offsetY = -(minY + maxY) * 0.5;
    this.scale = 1.8 / Math.max(w, h);

    gl.bindBuffer(gl.ARRAY_BUFFER, this.trackBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, trackLine, gl.STATIC_DRAW);
  }

  private resizeIfNeeded(): void {
    const dpr = Math.max(1, window.devicePixelRatio || 1);
    const width = Math.floor(this.canvas.clientWidth * dpr);
    const height = Math.floor(this.canvas.clientHeight * dpr);
    if (this.canvas.width !== width || this.canvas.height !== height) {
      this.canvas.width = width;
      this.canvas.height = height;
    }
  }

  render(cars: CarViews): void {
    this.resizeIfNeeded();

    const gl = this.gl;
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0.03, 0.04, 0.06, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    gl.useProgram(this.trackProgram);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.trackBuffer);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    gl.uniform2f(gl.getUniformLocation(this.trackProgram, 'uScale'), this.scale, this.scale);
    gl.uniform2f(gl.getUniformLocation(this.trackProgram, 'uOffset'), this.offsetX, this.offsetY);
    gl.drawArrays(gl.LINE_LOOP, 0, this.trackVerts);

    const packed = new Float32Array(cars.x.length * 2);
    for (let i = 0; i < cars.x.length; i += 1) {
      packed[i * 2 + 0] = cars.x[i];
      packed[i * 2 + 1] = cars.y[i];
    }

    gl.useProgram(this.carProgram);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.carBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, packed, gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    gl.uniform2f(gl.getUniformLocation(this.carProgram, 'uScale'), this.scale, this.scale);
    gl.uniform2f(gl.getUniformLocation(this.carProgram, 'uOffset'), this.offsetX, this.offsetY);
    gl.drawArrays(gl.POINTS, 0, cars.x.length);
  }
}
