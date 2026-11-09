#!/bin/bash
# Benchmarks de latence du carnet d'ordres, compilés en Release dans un dossier dédié.
set -e

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-release"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$BUILD_DIR" -t OrderBookBenchmark -j"$(nproc)"
"$BUILD_DIR/OrderBookBenchmark"
