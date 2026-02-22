#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-native"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DF1SIM_BUILD_WASM=OFF -DF1SIM_BUILD_CLI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" -j

echo "Native CLI: ${BUILD_DIR}/sim/f1sim_cli"
