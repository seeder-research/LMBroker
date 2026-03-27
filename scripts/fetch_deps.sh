#!/usr/bin/env bash
# Download vendored header-only dependencies into third_party/
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP="$ROOT/third_party"

echo "[deps] Fetching nlohmann/json v3.11.3..."
mkdir -p "$TP/nlohmann"
curl -fsSL \
  https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp \
  -o "$TP/nlohmann/json.hpp"

echo "[deps] Fetching cpp-httplib v0.15.3..."
mkdir -p "$TP/httplib"
curl -fsSL \
  https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.15.3/httplib.h \
  -o "$TP/httplib/httplib.h"

echo "[deps] Fetching spdlog v1.13.0..."
mkdir -p "$TP/spdlog"
curl -fsSL \
  https://github.com/gabime/spdlog/archive/refs/tags/v1.13.0.tar.gz \
  | tar -xz -C "$TP/spdlog" --strip-components=1

echo "[deps] All dependencies fetched into $TP"
