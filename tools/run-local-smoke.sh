#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-9000}"
FRAMES="${2:-100}"
TIMEOUT="${3:-30}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

python3 "${ROOT}/tools/run_local_smoke.py" --port "${PORT}" --frames "${FRAMES}" --timeout "${TIMEOUT}"
