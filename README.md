# Market maker DRL

Carnet d'ordres en C++20 alimenté par les données temps réel de Binance, et agents de trading Python (stratégies à règles et agents d'apprentissage par renforcement) qui passent leurs ordres sur le testnet Binance. Un dashboard web permet de piloter le tout.

## Structure

```
├── include/, src/          # Moteur C++ : OrderBook (matching) et LiveMarketData (flux Binance)
├── tests/                  # Tests fonctionnels (tests.cpp) et benchmarks de latence (benchmark.cpp)
├── agents/
│   ├── main.py             # Point d'entrée de l'agent live (variables STRATEGY, SYMBOL)
│   ├── agent.py            # TradingAgent : lit data/market_state.json, exécute sur Binance
│   ├── interfaces.py       # BaseStrategy / StrategySignal
│   ├── strategies/         # mean_reversion, rsi, bollinger, sma, random, a2c, dqn, ppo, sac, dreamer
│   ├── exchange/           # Client Binance testnet (config.py à créer depuis config.example.py)
│   ├── ppo/, dqn/          # Entraînement et évaluation PPO / DQN
│   ├── train_a2c.py, train_dreamer.py, train_rl.py
│   └── tests/              # Tests pytest des stratégies
├── models/                 # Poids des agents RL entraînés
├── eval/oos_eval.py        # Évaluation hors-échantillon de tous les agents
├── dashboard/              # Serveur HTTP (server.py) et interface web
├── rapport/                # Rapport, présentation et figures (LaTeX)
└── data/                   # Fichiers échangés à l'exécution et données historiques (non versionnés)
```

Le moteur C++ écrit l'état du carnet dans `data/market_state.json`. L'agent Python le lit, calcule un signal avec la stratégie choisie et envoie l'ordre sur le testnet.

## Installation

Prérequis : CMake 3.10+, un compilateur C++20 (GCC 10+, Clang 10+), les en-têtes OpenSSL et Python 3.10+. libcurl et nlohmann/json sont téléchargés par CMake.

```bash
./build.sh                          # compile build/LiveMarketData, OrderBookTests, OrderBookBenchmark

python3 -m venv eval_venv
eval_venv/bin/pip install torch --index-url https://download.pytorch.org/whl/cpu
eval_venv/bin/pip install -r requirements.txt

cp agents/exchange/config.example.py agents/exchange/config.py   # puis renseigner les clés testnet
```

## Utilisation

```bash
./run_dashboard.sh                  # dashboard sur http://127.0.0.1:8090 (identifiants par défaut : admin / admin)

# ou sans dashboard, dans deux terminaux :
./run_market.sh [SYMBOL] [REFRESH_SECONDS] [LEVELS]
./run_agent.sh [STRATEGY]           # ./run_agent.sh --help pour la liste

./run_oos_eval.sh                   # évaluation hors-échantillon (SOLUSDT, mars 2026)
python3 run_dreamer.py --fast       # entraînement Dreamer v3
make pdf                            # compile le rapport, la présentation et le rapport Dreamer
```

## Tests

```bash
# C++
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j
ctest --test-dir build-debug --output-on-failure

./benchmark.sh                      # benchmarks de latence (Release, build-release/)

# Python
eval_venv/bin/python -m pytest

# Dashboard (API + E2E Playwright si installé : npm ci && npx playwright install)
./scripts/test_dashboard_all.sh
```

Les tests `start_and_stop_simulation` et `clear_logs` du dashboard lancent un vrai agent sur le testnet et vident les logs de `data/`.

## Moteur de matching

- Types d'ordres : GoodTillCancel, Market, ImmediateOrCancel, FillOrKill, GoodForDay.
- Priorité prix-temps : les meilleurs niveaux de prix passent d'abord, puis FIFO au sein d'un niveau.
- Exécutions partielles pour tous les types sauf FillOrKill.
- Un ordre est exécuté quand son prix d'achat est ≥ au meilleur ask, ou son prix de vente ≤ au meilleur bid.

| Opération       | Complexité  |
|-----------------|-------------|
| Ajout d'ordre   | O(log n)    |
| Annulation      | O(1)        |
| Modification    | O(log n)    |
| Matching        | O(k log n)  |
| Snapshot        | O(m)        |

*n = nombre de niveaux de prix, k = nombre d'exécutions, m = nombre d'ordres.*

### Choix d'implémentation

- **Maps séparées pour bids et asks** : ordres de tri différents (décroissant / croissant) et accès direct au meilleur prix.
- **Itérateurs stockés dans chaque entrée** : suppression en O(1) dans la liste du niveau de prix lors d'une annulation.
- **`shared_ptr` pour les ordres** : un même ordre est référencé par l'index et par son niveau de prix sans copie.
- **Ordres Market** : convertis en ordres limite à prix extrême pour réutiliser la logique de matching.
- **GoodForDay** : annulés à l'heure configurée (15:59 par défaut).
