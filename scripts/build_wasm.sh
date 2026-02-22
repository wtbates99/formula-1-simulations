#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-wasm"
OUT_DIR="${ROOT_DIR}/ts/public/wasm"
EMSDK_DIR="${ROOT_DIR}/emsdk"

if ! command -v emcmake >/dev/null 2>&1 || ! command -v emmake >/dev/null 2>&1; then
  if [ -f "${EMSDK_DIR}/emsdk_env.sh" ]; then
    # shellcheck disable=SC1090
    source "${EMSDK_DIR}/emsdk_env.sh" >/dev/null
  fi
fi

if ! command -v emcmake >/dev/null 2>&1 || ! command -v emmake >/dev/null 2>&1; then
  echo "Emscripten not found. Install emsdk in ${EMSDK_DIR} or source emsdk first." >&2
  exit 1
fi

emcmake cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DF1SIM_BUILD_WASM=ON -DF1SIM_BUILD_CLI=OFF -DCMAKE_BUILD_TYPE=Release
emmake cmake --build "${BUILD_DIR}" -j

mkdir -p "${OUT_DIR}"
cp "${BUILD_DIR}/sim/f1sim.js" "${OUT_DIR}/f1sim.js"
cp "${BUILD_DIR}/sim/f1sim.wasm" "${OUT_DIR}/f1sim.wasm"

echo "WASM artifacts copied to ${OUT_DIR}"
