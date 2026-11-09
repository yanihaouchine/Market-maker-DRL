#!/bin/bash
# Script pour lancer l'agent Python avec une stratégie au choix

cd "$(dirname "$0")"

if ! command -v python3 &> /dev/null; then
    echo "Python3 n'est pas installé!"
    exit 1
fi

echo "Python trouvé: $(python3 --version)"

STRATEGY="${1:-mean_reversion}"

if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    echo ""
    echo "Usage: ./run_agent.sh [STRATEGY]"
    echo ""
    echo "Stratégies disponibles:"
    echo "  mean_reversion  - Mean Reversion (par défaut)"
    echo "  rsi             - RSI"
    echo "  bollinger       - Bandes de Bollinger"
    echo "  sma             - Moyenne mobile simple"
    echo "  random          - Aléatoire (référence)"
    echo "  a2c | dqn | ppo | sac - Agents RL (modèles dans models/)"
    echo ""
    echo "Exemples:"
    echo "  ./run_agent.sh              # Lance Mean Reversion"
    echo "  ./run_agent.sh rsi          # Lance RSI Strategy"
    echo "  ./run_agent.sh bollinger    # Lance Bollinger Bands"
    echo ""
    exit 0
fi

echo "Stratégie: $STRATEGY"
echo "Lancement de l'agent..."
echo ""

STRATEGY=$STRATEGY python3 -m agents.main
