#!/bin/bash
# Compile les cibles C++ (Release) dans build/.
set -e

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Configuration CMake..."
    cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
fi

echo "Compilation..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "Compilation réussie : build/LiveMarketData | build/OrderBookTests | build/OrderBookBenchmark"
