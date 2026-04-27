# F1 Replay

F1 Replay is a Svelte and Tauri experiment for browser and desktop Formula 1
telemetry visualization. It is part of the broader Formula 1 Simulations
workspace and is intended for replay UI exploration rather than as a standalone
published app.

## Requirements

- Node.js 18+
- npm
- Rust and the Tauri prerequisites for your operating system, if running the
  desktop shell

## Setup

```bash
npm install
```

## Web Development

```bash
npm run dev
```

## Desktop Development

```bash
npm run tauri dev
```

## Build

```bash
npm run build
```

For Tauri packaging:

```bash
npm run tauri build
```

## Validation

```bash
npm run check
```
