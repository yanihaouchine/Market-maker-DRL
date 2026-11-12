#include "OrderBook/OrderBook.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

Trades Orderbook::AddOrder(OrderPointer order) {
    core_.CheckDay();

    if (order->GetOrderType() == OrderType::Market) {
        const Side opposite = order->GetSide() == Side::Buy ? Side::Sell : Side::Buy;
        if (core_.SideEmpty(opposite)) {
            return {};
        }
        order->ToGoodTillCancel(order->GetSide() == Side::Buy
                                    ? std::numeric_limits<Price>::max()
                                    : std::numeric_limits<Price>::min());
    }

    Trades trades;
    const Quantity before = order->GetRemainingQuantity();
    const Quantity after = core_.Add(order->GetOrderType(), order->GetOrderId(), order->GetSide(),
                                     order->GetPrice(), before, trades);
    order->Fill(before - after);
    return trades;
}

Trades Orderbook::MatchOrder(OrderModify order) {
    Trades trades;
    const bool known = core_.Modify(order.GetOrderId(), order.GetSide(), order.GetPrice(),
                                    order.GetQuantity(), trades);
    if (known && order.GetQuantity() == 0) {
        // Comportement historique : l'ordre est annulé, puis la reconstruction échoue.
        throw std::invalid_argument("Order quantity must be greater than zero");
    }
    return trades;
}

OrderbookLevelInfos Orderbook::GetOrderInfos() const {
    LevelInfos bidInfos, askInfos;
    core_.AppendLevels(Side::Buy, bidInfos);
    core_.AppendLevels(Side::Sell, askInfos);
    return OrderbookLevelInfos(std::move(bidInfos), std::move(askInfos));
}

void Orderbook::ProcessNewOrder(const NewOrderMessage &msg) {
    if (msg.quantity == 0) {
        stats_.errors++;
        return;
    }
    scratchTrades_.clear();
    core_.Add(msg.orderType, msg.orderId, msg.side, msg.price, msg.quantity, scratchTrades_);
    stats_.newOrders++;
    stats_.trades += scratchTrades_.size();
}

void Orderbook::ProcessCancel(const CancelOrderMessage &msg) {
    core_.Cancel(msg.orderId);
    stats_.cancellations++;
}

void Orderbook::ProcessModify(const ModifyOrderMessage &msg) {
    scratchTrades_.clear();
    const bool known = core_.Modify(msg.orderId, msg.side, msg.newPrice, msg.newQuantity, scratchTrades_);
    if (known && msg.newQuantity == 0) {
        throw std::invalid_argument("Order quantity must be greater than zero");
    }
    stats_.modifications++;
}

void Orderbook::ProcessTrade(const TradeMessage &) {
    stats_.trades++;
}

void Orderbook::ProcessSnapshot(const BookSnapshotMessage &msg) {
    core_.Clear();

    OrderId syntheticId = 0x8000000000000000ULL;

    for (const auto &level: msg.bids) {
        if (level.quantity == 0) continue;
        core_.InsertResting(OrderType::GoodTillCancel, syntheticId++, Side::Buy, level.price, level.quantity);
    }

    for (const auto &level: msg.asks) {
        if (level.quantity == 0) continue;
        core_.InsertResting(OrderType::GoodTillCancel, syntheticId++, Side::Sell, level.price, level.quantity);
    }

    isInitialized_ = true;
    lastSequenceNumber_ = msg.sequenceNumber;
    stats_.snapshots++;
}

bool Orderbook::ProcessMarketData(const MarketDataMessage &message) {
    auto startTime = std::chrono::high_resolution_clock::now();

    try {
        std::visit([this](auto &&msg) {
            using T = std::decay_t<decltype(msg)>;

            if constexpr (std::is_same_v<T, NewOrderMessage>) {
                ProcessNewOrder(msg);
            } else if constexpr (std::is_same_v<T, CancelOrderMessage>) {
                ProcessCancel(msg);
            } else if constexpr (std::is_same_v<T, ModifyOrderMessage>) {
                ProcessModify(msg);
            } else if constexpr (std::is_same_v<T, TradeMessage>) {
                ProcessTrade(msg);
            } else if constexpr (std::is_same_v<T, BookSnapshotMessage>) {
                ProcessSnapshot(msg);
            }
        }, message);

        auto endTime = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        stats_.messagesProcessed++;
        stats_.totalProcessingTime += latency;
        stats_.maxLatency = std::max(stats_.maxLatency, latency);
        stats_.minLatency = std::min(stats_.minLatency, latency);

        return true;
    } catch (...) {
        stats_.errors++;
        return false;
    }
}

size_t Orderbook::ProcessMarketDataBatch(const std::vector<MarketDataMessage> &messages) {
    size_t successCount = 0;
    for (const auto &msg: messages) {
        if (ProcessMarketData(msg)) {
            successCount++;
        }
    }
    return successCount;
}
