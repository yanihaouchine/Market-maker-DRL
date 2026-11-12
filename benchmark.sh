#!/bin/bash
# Benchmarks du carnet d'ordres, compilés en Release dans un dossier dédié.
#   ./benchmark.sh            latences par opération (API de compatibilité) puis banc du cœur
#   ./benchmark.sh core ...   banc du cœur seul ; les arguments sont passés à OrderBookBenchCore
#                             (ex. : ./benchmark.sh core --engine=dense-direct --scenario=mixte)
# ORDERBOOK_NATIVE=ON ./benchmark.sh compile pour le processeur local (-march=native + LTO).
set -e

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-release"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
      -DORDERBOOK_NATIVE="${ORDERBOOK_NATIVE:-OFF}" > /dev/null
cmake --build "$BUILD_DIR" -t OrderBookBenchmark -j"$(nproc)"
cmake --build "$BUILD_DIR" -t OrderBookBenchCore -j"$(nproc)"

# Épingler le processus sur un cœur réduit fortement la dispersion des mesures.
PIN=""
if command -v taskset > /dev/null; then
    PIN="taskset -c $(( $(nproc) - 1 ))"
fi

if [ "$1" = "core" ]; then
    shift
    $PIN "$BUILD_DIR/OrderBookBenchCore" "$@"
else
    $PIN "$BUILD_DIR/OrderBookBenchmark"
    $PIN "$BUILD_DIR/OrderBookBenchCore" --engine=dense-direct
fi
