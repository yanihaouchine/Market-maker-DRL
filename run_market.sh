#!/bin/bash
# Script pour lancer le LiveMarketData C++

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"

# Afficher l'aide si demandé
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    echo ""
    echo "Usage: ./run_market.sh [OPTIONS]"
    echo ""
    echo "Description:"
    echo "  Lance le fetcher C++ qui récupère les données du marché"
    echo "  depuis Binance testnet et les écrit dans data/market_state.json"
    echo ""
    echo "Options:"
    echo "  -h, --help      Affiche cette aide"
    echo ""
    echo "Paramètres (optionnels):"
    echo "  SYMBOL          Symbole à tracker (défaut: SOLUSDT)"
    echo "  UPDATE_FREQ     Fréquence en secondes (défaut: 1)"
    echo "  DEPTH           Profondeur du carnet d'ordres (défaut: 50)"
    echo ""
    echo "Exemples:"
    echo "  ./run_market.sh                    # Lance SOLUSDT"
    echo "  ./run_market.sh ETHUSDT            # Lance ETHUSDT"
    echo "  ./run_market.sh BTCUSDT 2 20       # BTCUSDT, mise à jour tous les 2s, profondeur 20"
    echo ""
    exit 0
fi

# Récupérer les paramètres (avec valeurs par défaut)
SYMBOL="${1:-SOLUSDT}"
UPDATE_FREQ="${2:-1}"
DEPTH="${3:-50}"

echo "🚀 Lancement du LiveMarketData..."
echo "📊 Symbole: $SYMBOL"
echo "⏱️  Fréquence: ${UPDATE_FREQ}s"
echo "📈 Profondeur: $DEPTH"
echo ""

cd "$PROJECT_ROOT" || exit 1
./build/LiveMarketData "$SYMBOL" "$UPDATE_FREQ" "$DEPTH"
