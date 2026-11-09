#!/bin/bash
# Évaluation hors-échantillon de tous les agents (règles + RL) sur SOLUSDT mars 2026.
#
# - Crée le venv eval_venv/ si manquant
# - Installe les dépendances (torch CPU, gymnasium, pandas, matplotlib)
# - Télécharge SOLUSDT-aggTrades-2026-03.csv depuis Binance Vision si manquant
# - Lance eval/oos_eval.py
#
# Usage: ./run_oos_eval.sh

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$PROJECT_ROOT"

VENV="eval_venv"
DATA_DIR="data"
CSV="$DATA_DIR/SOLUSDT-aggTrades-2026-03.csv"
ZIP_URL="https://data.binance.vision/data/spot/monthly/aggTrades/SOLUSDT/SOLUSDT-aggTrades-2026-03.zip"

# 1. venv
if [ ! -d "$VENV" ]; then
    echo "[setup] création de $VENV ..."
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install --quiet --upgrade pip
    "$VENV/bin/pip" install --quiet torch --index-url https://download.pytorch.org/whl/cpu
    "$VENV/bin/pip" install --quiet gymnasium pandas numpy matplotlib
    echo "[setup] dépendances installées"
fi

# 2. Données mars 2026
if [ ! -f "$CSV" ]; then
    mkdir -p "$DATA_DIR"
    cd "$DATA_DIR"
    echo "[data] téléchargement de SOLUSDT mars 2026 ..."
    curl -fsSL -o "SOLUSDT-aggTrades-2026-03.zip" "$ZIP_URL"
    echo "[data] décompression ..."
    unzip -o -q SOLUSDT-aggTrades-2026-03.zip
    rm -f SOLUSDT-aggTrades-2026-03.zip
    cd "$PROJECT_ROOT"
fi

# 3. Lancement
echo "[run] lancement de eval/oos_eval.py ..."
"$VENV/bin/python" eval/oos_eval.py
