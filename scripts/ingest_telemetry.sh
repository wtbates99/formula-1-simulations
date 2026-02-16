#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build-cmake}"
SEASON="${2:-2024}"
ROUND="${3:-1}"
DB="${4:-telemetry.db}"
PAGE_SIZE="${5:-1000}"

if [[ ! -x "$BUILD_DIR/telemetry_ingest" ]]; then
  echo "[f1sim] telemetry_ingest not found, building first..."
  cmake -S . -B "$BUILD_DIR"
  cmake --build "$BUILD_DIR" -j
fi

"$BUILD_DIR/telemetry_ingest" \
  --season "$SEASON" \
  --round "$ROUND" \
  --page-size "$PAGE_SIZE" \
  --db "$DB"
