#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python "${ROOT_DIR}/scripts/sqlite_api.py" \
  --db-path "${ROOT_DIR}/f1.sqlite" \
  --host 0.0.0.0 \
  --port 8000
