# Trading System - Order Book + Live Market Agent

Système complet de trading avec **Order Book en C++** + **Agent de trading en Python** connecté à **Binance Testnet**.

## 🎯 Architecture

```
┌──────────────────┐
│ Binance Testnet  │ ← Source unique des données
│   (Public API)   │
└────────┬─────────┘
         │
         ├─────────▶ Programme C++ (crée l'orderbook)
         │           └─▶ market_state.json
         │
         └─────────▶ Agent Python (lit market_state.json)
                    └─▶ Exécute les ordres directement
```

## � Structure du Projet

```
live/
├── agents/                          # Agent de trading Python
│   ├── __init__.py
│   ├── agent.py                    # Boucle principale de l'agent
│   ├── display.py                  # Affichage des données
│   ├── main.py                     # Entrée de l'application
│   ├── market_data.py              # Gestion des données de marché
│   ├── trading_types.py            # Types de données
│   ├── exchange/                   # Client Binance
│   │   ├── __init__.py
│   │   ├── binance_client.py       # Intégration Binance
│   │   ├── config.example.py       # Template de configuration
│   │   └── config.py               # Configuration (ignorée par Git)
│   ├── strategies/                 # Stratégies de trading
│   │   ├── __init__.py
│   │   ├── bollinger_bands.py
│   │   ├── mean_reversion.py
│   │   └── rsi.py
│   └── tests/                      # Tests unitaires Python
│       ├── __init__.py
│       ├── test_binance_order.py
│       └── test_system.py
├── include/                        # Headers C++
│   ├── Core/
│   │   ├── Constants.h
│   │   ├── OrderType.h
│   │   └── Types.h
│   ├── LiveMarketData/
│   │   ├── Display.h
│   │   ├── Fetcher.h
│   │   └── Parser.h
│   ├── MarketData/
│   │   └── MarketDataFeed.h
│   └── OrderBook/
│       ├── ExchangeRules.h
│       ├── LevelInfo.h
│       ├── Order.h
│       ├── OrderBook.h
│       ├── OrderModify.h
│       └── Trade.h
├── src/                            # Implémentations C++
│   ├── LiveMarketData/
│   │   ├── Display.cpp
│   │   ├── Fetcher.cpp
│   │   ├── Main.cpp
│   │   └── Parser.cpp
│   └── OrderBook/
│       ├── Order.cpp
│       ├── OrderBook.cpp
│       ├── OrderModify.cpp
│       └── Trade.cpp
├── tests/                          # Tests C++
│   └── tests.cpp
├── data/                           # Données de marché (ignorées par Git)
│   └── market_state.json
├── build/                          # Dossier de compilation (ignoré par Git)
├── CMakeLists.txt                  # Configuration CMake
├── build.sh                        # Script de build
├── run_agent.sh                    # Lancer l'agent Python
├── run_market.sh                   # Lancer le fetcher C++
├── integration_test.sh             # Test complet du système
├── get_orders.sh                   # Récupérer les ordres
├── .gitignore                      # Configuration Git
└── README.md
```

## �📊 3 Stratégies de Trading

### 1. **Mean Reversion** (défaut)
- Achète quand prix < SMA - 1%
- Vend quand prix > SMA + 1%
- Basé sur 3 derniers prix
```bash
./run_agent.sh
```

### 2. **RSI (Relative Strength Index)**
- Achète si RSI < 30 (survendu)
- Vend si RSI > 70 (suracheté)
- Basé sur 14 derniers prix
```bash
./run_agent.sh rsi
```

### 3. **Bollinger Bands**
- Achète si prix touche bande inférieure
- Vend si prix touche bande supérieure
- Basé sur 20 derniers prix avec écart-type
```bash
./run_agent.sh bollinger
```

## 🚀 Configuration Initiale

1. Obtenez vos clés API: https://testnet.binance.vision/
2. Configurez:
```bash
cp agents/exchange/config.example.py agents/exchange/config.py
# Éditez avec vos clés
```

## 💻 Compilation

```bash
# Première fois
mkdir build && cd build && cmake .. && make

# Après modifications
./build.sh
```

## ▶️ Lancer le Système

### ⚠️ Important: Démarrer en 2 Terminaux

**Terminal 1 - Données de marché (C++)**:
```bash
./run_market.sh [SYMBOL] [REFRESH_SECONDS] [DEPTH_LEVELS]
# Exemples:
./run_market.sh SOLUSDT 1 10
./run_market.sh BTCUSDT 2 20
```

**Terminal 2 - Agent (Python)**:
```bash
./run_agent.sh                    # Mean Reversion (défaut)
./run_agent.sh rsi       # RSI Strategy
./run_agent.sh bollinger # Bollinger Bands
```

### Option 3: Test Complet (3 stratégies en parallèle)
```bash
bash integration_test.sh
```

## 🧪 Tests Unitaires

```bash
# Test système complet
python3 -m agents.tests.test_system

# Test individual
python3 -m agents.tests.test_binance_order

# Test OrderBook 
./build/OrderBookTests
```

## 📚 Documentation Complète OrderBook

### 🔄 Matching Logic

Orders match when:

- Buy price ≥ Best ask price, or
- Sell price ≤ Best bid price

Matching proceeds in price-time priority:

1. Best price levels matched first
2. Within a price level, earliest orders matched first (FIFO)
3. Partial fills supported for all order types except FillOrKill

### ⚡ Performance Characteristics

| Operation      | Complexity | Measured Throughput |
|----------------|------------|---------------------|
| Add Order      | O(log n)   | ~400K ops/sec       |
| Cancel Order   | O(1)       | ~2M ops/sec         |
| Modify Order   | O(log n)   | ~270K ops/sec       |
| Match Orders   | O(k log n) | ~350K matches/sec   |
| Get Order Info | O(m)       | ~500K snapshots/sec |

*n = number of price levels, k = number of matches, m = number of orders*

Benchmarks run on typical development hardware. Actual performance depends on system configuration and workload
characteristics.

## 🏗️ Implementation Notes

### 💡 Design Decisions

**Why separate bids/asks maps?** Allows different sorting orders (descending for bids, ascending for asks) and
simplifies best bid/ask access.

**Why store iterators in OrderEntry?** Enables O(1) removal from the price level list when canceling orders.

**Why shared_ptr for orders?** Allows orders to be referenced in multiple places (main map, price level list) without
copying. Trade-off between memory overhead and simplicity.

**Market order conversion:** Market orders are converted to limit orders at extreme prices (max/min) to reuse the
matching logic.

### 📋 Order Type Behavior

- **GoodTillCancel**: Remains active until filled or cancelled
- **Market**: Immediately converted to aggressive limit order
- **ImmediateOrCancel**: Partial fills accepted, unfilled portion cancelled
- **FillOrKill**: All-or-nothing execution, rejected if can't fill completely
- **GoodForDay**: Cancelled at configured time (default 15:59)


