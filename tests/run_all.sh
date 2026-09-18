#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}" -DLUAU_BUILD_CLI=OFF -DLUAU_BUILD_TESTS=OFF
cmake --build "$BUILD" --parallel "${JOBS:-2}"
ctest --test-dir "$BUILD" --output-on-failure
