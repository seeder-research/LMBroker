#!/usr/bin/env bash
# Run the test suite.
# Usage:
#   ./scripts/run_tests.sh              # unit tests only
#   TEST_DB_CONNSTR="..." ./scripts/run_tests.sh --integration
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
RUN_INTEGRATION=0

for arg in "$@"; do
  [[ "$arg" == "--integration" ]] && RUN_INTEGRATION=1
done

if [[ ! -f "$BUILD/flexlm-broker" ]]; then
  echo "[test] Binary not found — running build first..."
  "$ROOT/scripts/build.sh"
fi

cd "$BUILD"

echo "[test] Running unit tests..."
ctest --output-on-failure -E integration

if [[ $RUN_INTEGRATION -eq 1 ]]; then
  if [[ -z "${TEST_DB_CONNSTR:-}" ]]; then
    echo "[test] ERROR: TEST_DB_CONNSTR must be set for integration tests."
    exit 1
  fi
  echo "[test] Running integration tests..."
  ctest --output-on-failure -L integration
fi

echo "[test] All tests passed."
