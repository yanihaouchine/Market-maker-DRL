#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Core/OrderType.h"
#include "Core/Types.h"
#include "MarketData/MarketDataFeed.h"
#include "OrderBook/BookCore.h"
#include "OrderBook/ExchangeRules.h"
#include "OrderBook/LevelInfo.h"
#include "OrderBook/Order.h"
#include "OrderBook/OrderModify.h"
#include "OrderBook/PriceLevels.h"
#include "OrderBook/Trade.h"

#ifndef ORDERBOOK_LEVELS
#define ORDERBOOK_LEVELS ob::DenseLevels
#endif

class Orderbook {
public:
    using Core = ob::BookCore<ORDERBOOK_LEVELS>;

    void SetExchangeRules(const ExchangeRules &rules) { core_.SetExchangeRules(rules); }
    const ExchangeRules &GetExchangeRules() const { return core_.GetExchangeRules(); }
    void SetDayResetTime(int hour, int minute = 59) { core_.SetDayResetTime(hour, minute); }

    // --- API rapide : aucun shared_ptr, aucune allocation sur le chemin chaud. ---

    // Ajoute un ordre ; les trades sont ajoutés à la fin de `trades` (vecteur à réutiliser d'un
    // appel à l'autre). Renvoie la quantité non exécutée.
    Quantity AddOrder(OrderType type, OrderId id, Side side, Price price, Quantity quantity, Trades &trades) {
        return core_.Add(type, id, side, price, quantity, trades);
    }

    bool CancelOrder(OrderId orderId) { return core_.Cancel(orderId); }

    // Annule puis réinsère l'ordre avec son type d'origine. Renvoie false si l'ordre est inconnu.
    bool ModifyOrder(OrderId id, Side side, Price price, Quantity quantity, Trades &trades) {
        return core_.Modify(id, side, price, quantity, trades);
    }

    // Pré-dimensionne le pool et la table d'identifiants (évite les réallocations en cours de route).
    void Reserve(std::size_t orders) { core_.Reserve(orders); }

    // --- API de compatibilité. ---

    Trades AddOrder(OrderPointer order);
    Trades MatchOrder(OrderModify order);

    std::size_t Size() const { return core_.Size(); }
    OrderbookLevelInfos GetOrderInfos() const;

    bool ProcessMarketData(const MarketDataMessage &message);
    size_t ProcessMarketDataBatch(const std::vector<MarketDataMessage> &messages);
    const MarketDataStats &GetMarketDataStats() const { return stats_; }
    void ResetMarketDataStats() { stats_.Reset(); }
    bool IsInitialized() const { return isInitialized_; }
    uint64_t GetLastSequenceNumber() const { return lastSequenceNumber_; }

private:
    void ProcessNewOrder(const NewOrderMessage &msg);
    void ProcessCancel(const CancelOrderMessage &msg);
    void ProcessModify(const ModifyOrderMessage &msg);
    void ProcessTrade(const TradeMessage &msg);
    void ProcessSnapshot(const BookSnapshotMessage &msg);

    Core core_;
    Trades scratchTrades_; // tampon réutilisé par le flux de marché

    MarketDataStats stats_;
    uint64_t lastSequenceNumber_ = 0;
    bool isInitialized_ = false;
};
