#!/usr/bin/env bash
# Configure and build flexlm-broker
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc)}"

echo "[build] Build type: $BUILD_TYPE"
echo "[build] Parallel jobs: $JOBS"

mkdir -p "$BUILD"
cd "$BUILD"

cmake "$ROOT" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DBUILD_TESTS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

make -j"$JOBS"

echo ""
echo "[build] Binary: $BUILD/flexlm-broker"
echo "[build] Done."
