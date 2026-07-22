#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cmake -S "${ROOT}" -B "${ROOT}/${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "${ROOT}/${BUILD_DIR}"
ctest --test-dir "${ROOT}/${BUILD_DIR}" --output-on-failure
