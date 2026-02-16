#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build-cmake}"

cmake -S . -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" -j

if [[ -f "$BUILD_DIR/compile_commands.json" ]]; then
  cp "$BUILD_DIR/compile_commands.json" ./compile_commands.json
  echo "[f1sim] Wrote compile_commands.json to project root"
fi

echo "[f1sim] Built targets in $BUILD_DIR"
