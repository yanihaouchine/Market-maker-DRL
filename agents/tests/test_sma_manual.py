from agents.strategies.sma import SMAStrategy
from agents.types import Side


def market(price_cents):
    # MarketData divise mid_price par 100 : les prix sont envoyés en cents.
    return {"mid_price": price_cents, "bid_price": price_cents - 10, "ask_price": price_cents + 10}


def test_sma_buys_after_crash():
    strategy = SMAStrategy("SOLUSDT", quantity_per_trade=10, threshold=0.01)

    for _ in range(3):
        strategy.update_data(market(10000))
    assert strategy.get_status().get("sma") == 100.0

    # 100$ -> 90$ : déviation de -10 %, sous le seuil de -1 %.
    strategy.update_data(market(9000))
    signal = strategy.compute_signal()

    assert signal.side == Side.BUY, signal.reason
