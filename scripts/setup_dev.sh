#!/usr/bin/env bash
set -euo pipefail

echo "[f1sim] Installing C++ build dependencies..."
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  libsqlite3-dev \
  libcurl4-openssl-dev

echo "[f1sim] Optional viewer dependency..."
echo "Install raylib if you want sim_viewer:"
echo "  sudo apt install -y libraylib-dev"

echo "[f1sim] Done."
