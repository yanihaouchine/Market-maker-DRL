# Market maker DRL

Carnet d'ordres en C++20 alimenté par les données temps réel de Binance, et agents de trading Python (stratégies à règles et agents d'apprentissage par renforcement) qui passent leurs ordres sur le testnet Binance. Un dashboard web permet de piloter le tout.

## Structure

```
├── include/, src/          # Moteur C++ : OrderBook (matching) et LiveMarketData (flux Binance)
├── tests/                  # Tests fonctionnels (tests.cpp), test différentiel (differential.cpp),
│                           # benchmarks (benchmark.cpp, bench_core.cpp) et implémentation d'origine (reference/)
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
./build.sh                          # compile build/LiveMarketData, les tests et les benchmarks

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

./benchmark.sh                      # benchmarks (Release, build-release/)
./benchmark.sh core --scenario=mixte   # banc du cœur seul, arguments passés à OrderBookBenchCore

# Python
eval_venv/bin/python -m pytest

# Dashboard (API + E2E Playwright si installé : npm ci && npx playwright install)
./scripts/test_dashboard_all.sh
```

Les tests `start_and_stop_simulation` et `clear_logs` du dashboard lancent un vrai agent sur le testnet et vident les logs de `data/`.

`ctest` lance les tests fonctionnels et le test différentiel : celui-ci rejoue un million d'opérations aléatoires (tous les types d'ordres, annulations, modifications, instantanés) sur le moteur et sur l'implémentation d'origine conservée dans `tests/reference/`, et exige des trades, des tailles et des niveaux identiques.

## Moteur de matching

- Types d'ordres : GoodTillCancel, Market, ImmediateOrCancel, FillOrKill, GoodForDay.
- Priorité prix-temps : les meilleurs niveaux de prix passent d'abord, puis FIFO au sein d'un niveau.
- Exécutions partielles pour tous les types sauf FillOrKill.
- Un ordre est exécuté quand son prix d'achat est ≥ au meilleur ask, ou son prix de vente ≤ au meilleur bid.

| Opération       | Complexité          |
|-----------------|---------------------|
| Ajout d'ordre   | O(1)                |
| Annulation      | O(1)                |
| Modification    | O(1)                |
| Matching        | O(k)                |
| Snapshot        | O(n)                |

*n = nombre de niveaux de prix, k = nombre d'exécutions. Ces bornes valent pour les prix situés dans la fenêtre dense (65 536 ticks autour du premier prix reçu) ; au-delà, la recherche d'un niveau est en O(log n) et sa création en O(n).*

### Deux API

```cpp
Orderbook book;
Trades trades;                      // tampon réutilisé d'un appel à l'autre

// API rapide : aucune allocation sur le chemin chaud.
Quantity reste = book.AddOrder(OrderType::GoodTillCancel, id, Side::Buy, prix, quantite, trades);
book.ModifyOrder(id, Side::Buy, nouveauPrix, nouvelleQuantite, trades);
book.CancelOrder(id);

// API de compatibilité : même comportement, un shared_ptr et un vecteur alloués par appel.
Trades t = book.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, id, Side::Buy, prix, quantite));
```

Avec l'API de compatibilité, l'objet `Order` de l'appelant reflète l'exécution immédiate de l'ordre ; les exécutions ultérieures de son reliquat inscrit au carnet ne lui sont plus reportées (le moteur garde sa propre copie compacte).

### Choix d'implémentation

Chaque choix a été retenu sur mesure (`tests/bench_core.cpp`, Intel Celeron N4500, L1d 32 Kio, L2 1,5 Mio, L3 4 Mio).

- **Ordres de 32 octets dans un pool contigu** (`OrderPool.h`) : deux ordres par ligne de cache, files FIFO intrusives par indices 32 bits, emplacements libérés réutilisés en LIFO. Plus de `shared_ptr`, de `std::list` ni de nœud de tas par ordre.
- **Niveaux de prix en fenêtre dense** (`PriceLevels.h`) : tableau indexé directement par le prix et bitmap hiérarchique 64-aire à trois étages pour retrouver le meilleur niveau. Flux mixte, 100 000 ordres sur 20 000 niveaux : 179 ns par opération, contre 424 ns avec `std::map` et 1 396 ns avec un vecteur trié. Les prix hors fenêtre (dont les prix extrêmes des ordres Market) vont dans un vecteur trié.
- **Quantité agrégée par niveau** : le snapshot ne parcourt plus les ordres (0,1 µs pour 10 000 ordres, contre 168 µs).
- **Table d'identifiants à accès direct avec débordement** (`IdTable.h`) : case `id & masque` sans sondage ; en cas de collision, repli sur une table hachée à adressage ouvert. Identifiants séquentiels : ajout à 53 ns contre 110 ns avec un hachage de Fibonacci ; identifiants aléatoires : 5 à 20 % plus lent que le hachage seul. Un hachage identité avec sondage linéaire a été écarté : aussi rapide à l'ajout, mais 8 fois plus lent au balayage (longues grappes à décaler à chaque suppression).
- **Pages de 2 Mio** pour le pool et la table au-delà de 2 Mio : −19 % sur un carnet d'un million d'ordres, défauts dTLB divisés par 4 à 30.
- **Préchargement de l'ordre suivant** dans la boucle de matching : −10 % sur le balayage d'un carnet d'un million d'ordres, neutre ailleurs.
- **GoodForDay** : annulés à l'heure configurée (15:59 par défaut). L'heure de la prochaine remise à zéro est mise en cache ; le chemin chaud ne paie qu'une lecture d'horloge grossière (3 à 7 ns). Une variante par `rdtsc` n'apportait rien.
- **Options de compilation** : `-O2`, `-O3`, `-march=native`, LTO et PGO se tiennent dans 3 à 5 % (le moteur est limité par les accès mémoire). `-O3` par défaut ; `-DORDERBOOK_NATIVE=ON` ajoute `-march=native` et LTO ; le PGO n'a pas été retenu.
- **Ordres Market** : convertis en ordres limite à prix extrême pour réutiliser la logique de matching.

### Résultats

Latence moyenne par opération de `tests/benchmark.cpp` (API de compatibilité, horloge comprise) :

| Opération                          | Avant     | Après    |
|------------------------------------|-----------|----------|
| AddOrder (100 000 ordres)          | 193,8 µs  | 0,17 µs  |
| CancelOrder (100 000 ordres)       | 0,47 µs   | 0,12 µs  |
| ModifyOrder                        | 96,0 µs   | 0,08 µs  |
| Matching (1 trade)                 | 3,88 µs   | 0,17 µs  |
| Matching profond (1 000 trades)    | 98,9 µs   | 10,9 µs  |
| GetOrderInfos (10 000 ordres)      | 168,0 µs  | 0,10 µs  |
| Benchmark complet                  | 150 s     | 0,39 s   |

Flux mixte de `tests/bench_core.cpp` (1 000 ordres au carnet, 200 000 opérations) :

| Moteur                  | ns/op | Instructions/op | Défauts L1d (cachegrind) |
|-------------------------|-------|-----------------|--------------------------|
| Origine                 | 6 785 | ~14 500         | 223,8 M                  |
| Optimisé, compatibilité | 71    | ~600            | —                        |
| Optimisé, API rapide    | 48    | ~390            | 0,9 M                    |
