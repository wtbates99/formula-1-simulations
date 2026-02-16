#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build-cmake}"
shift || true

if [[ ! -x "$BUILD_DIR/sim_cli" ]]; then
  echo "[f1sim] sim_cli not found, building first..."
  cmake -S . -B "$BUILD_DIR"
  cmake --build "$BUILD_DIR" -j
fi

"$BUILD_DIR/sim_cli" "$@"
