#!/usr/bin/env python3
"""
Entry point for the trading agent (Binance testnet).
Choose the strategy with the STRATEGY environment variable (default: mean_reversion)
and the pair with SYMBOL.
"""

import importlib
import os
import traceback

from .agent import TradingAgent
from .exchange.config import BINANCE_TESTNET_API_KEY, BINANCE_TESTNET_API_SECRET

STRATEGIES = {
    "mean_reversion": (".strategies.mean_reversion", "MeanReversionStrategy"),
    "rsi": (".strategies.rsi", "RSIStrategy"),
    "bollinger": (".strategies.bollinger_bands", "BollingerBandsStrategy"),
    "sma": (".strategies.sma", "SMAStrategy"),
    "random": (".strategies.random_coin", "RandomStrategy"),
    "a2c": (".strategies.a2c_rl", "A2CStrategy"),
    "dqn": (".strategies.dqn_agent_faster", "DQNStrategy"),
    "ppo": (".strategies.PPO_agent", "PPOStrategy"),
    "sac": (".strategies.sac_agent", "SACStrategy"),
}


def main():
    if BINANCE_TESTNET_API_KEY == "your_api_key_here" or not BINANCE_TESTNET_API_KEY:
        print("Error: Please configure your Binance testnet API keys in agents/exchange/config.py")
        print("   You can get them from: https://testnet.binance.vision/")
        return

    strategy_name = os.getenv("STRATEGY", "mean_reversion").lower()
    module_path, class_name = STRATEGIES.get(strategy_name, STRATEGIES["mean_reversion"])

    try:
        strategy_class = getattr(importlib.import_module(module_path, package="agents"), class_name)
    except ImportError as e:
        print(f"{strategy_name.upper()} import failed: {e}")
        return
    print(f"Using {class_name}")

    try:
        agent = TradingAgent(
            symbol=os.getenv("SYMBOL"),
            strategy_class=strategy_class,
            api_key=BINANCE_TESTNET_API_KEY,
            api_secret=BINANCE_TESTNET_API_SECRET,
        )
        agent.run()
    except Exception:
        print("Failed to start agent:")
        traceback.print_exc()


if __name__ == "__main__":
    main()
