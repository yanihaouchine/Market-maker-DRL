// Test différentiel : rejoue des flux aléatoires sur l'implémentation d'origine
// (tests/reference/RefOrderBook.h) et sur le moteur optimisé, et exige des résultats identiques
// (trades, taille du carnet, niveaux agrégés, état de l'ordre entrant).
//
// Usage : OrderBookDifferential [opérations par configuration] [graine]

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

#include "OrderBook/Order.h"
#include "OrderBook/OrderBook.h"
#include "reference/RefOrderBook.h"

namespace {
struct Config {
    const char *name;
    Price mid;
    Price halfRange; // les prix sont tirés dans [mid - halfRange, mid + halfRange]
    ExchangeRules rules;
    unsigned cancelWeight; // sur 100 : part d'annulations (règle la profondeur du carnet)
};

[[noreturn]] void Fail(const Config &config, std::uint64_t seed, std::size_t op, const char *what) {
    std::cerr << "\nDIVERGENCE [" << config.name << "] graine=" << seed << " op=" << op << " : " << what << "\n";
    std::abort();
}

bool SameTrades(const Trades &a, const Trades &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const TradeInfo &ab = a[i].GetBidTrade(), &bb = b[i].GetBidTrade();
        const TradeInfo &aa = a[i].GetAskTrade(), &ba = b[i].GetAskTrade();
        if (ab.orderId_ != bb.orderId_ || ab.price_ != bb.price_ || ab.quantity_ != bb.quantity_) return false;
        if (aa.orderId_ != ba.orderId_ || aa.price_ != ba.price_ || aa.quantity_ != ba.quantity_) return false;
    }
    return true;
}

bool SameLevels(const LevelInfos &a, const LevelInfos &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].price_ != b[i].price_ || a[i].quantity_ != b[i].quantity_) return false;
    }
    return true;
}

std::uint64_t RunConfig(const Config &config, std::uint64_t seed, std::size_t operations) {
    ref::Orderbook expected;
    Orderbook actual;
    expected.SetExchangeRules(config.rules);
    actual.SetExchangeRules(config.rules);

    // Échéance GoodForDay placée à 12 h de l'heure courante : aucune remise à zéro pendant le test.
    const std::time_t now = std::time(nullptr);
    const int farHour = (std::localtime(&now)->tm_hour + 12) % 24;
    expected.SetDayResetTime(farHour, 0);
    actual.SetDayResetTime(farHour, 0);

    std::mt19937_64 gen(seed);
    auto pick = [&gen](std::uint64_t n) { return gen() % n; };

    std::vector<OrderId> known; // identifiants déjà émis (vivants ou non)
    OrderId nextId = 1;
    std::uint64_t tradeCount = 0;
    const Price tick = config.rules.tickSize;
    const Quantity lot = config.rules.lotSize;

    auto randomPrice = [&]() -> Price {
        const std::uint64_t r = pick(1000);
        if (r < 3) return std::numeric_limits<Price>::max();
        if (r < 6) return std::numeric_limits<Price>::min();
        if (r < 10) return 0;
        Price p = config.mid - config.halfRange + static_cast<Price>(pick(2 * config.halfRange + 1));
        if (r >= 40) p = (p / tick) * tick; // 3 % de prix hors pas de cotation
        return p;
    };
    auto randomQuantity = [&]() -> Quantity {
        const std::uint64_t r = pick(1000);
        if (r < 5) return config.rules.maxQuantity + 1;
        Quantity q = static_cast<Quantity>(1 + pick(60));
        if (r >= 30) q = std::max<Quantity>(lot, (q / lot) * lot);
        return q;
    };

    for (std::size_t op = 0; op < operations; ++op) {
        const std::uint64_t action = pick(100);

        if (op % 20000 == 19999) {
            // Instantané, parfois croisé, parfois avec des niveaux vides ou en double.
            BookSnapshotMessage snapshot;
            snapshot.sequenceNumber = op;
            const int levels = static_cast<int>(pick(30));
            for (int i = 0; i < levels; ++i) {
                snapshot.bids.push_back({static_cast<Price>(config.mid - static_cast<Price>(pick(40)) + 3),
                                         static_cast<Quantity>(pick(50)), 1});
                snapshot.asks.push_back({static_cast<Price>(config.mid + static_cast<Price>(pick(40)) - 3),
                                         static_cast<Quantity>(pick(50)), 1});
            }
            if (expected.ProcessMarketData(snapshot) != actual.ProcessMarketData(snapshot))
                Fail(config, seed, op, "ProcessMarketData(snapshot)");
            known.clear();
        } else if (action < 100 - config.cancelWeight - 12 || known.empty()) {
            const std::uint64_t t = pick(100);
            const OrderType type = t < 55 ? OrderType::GoodTillCancel
                                   : t < 65 ? OrderType::GoodForDay
                                   : t < 77 ? OrderType::ImmediateOrCancel
                                   : t < 88 ? OrderType::FillOrKill
                                   : OrderType::Market;
            const Side side = pick(2) ? Side::Buy : Side::Sell;
            OrderId id = nextId++;
            if (!known.empty() && pick(100) < 3) id = known[pick(known.size())]; // doublon possible
            const Quantity quantity = randomQuantity();

            std::shared_ptr<ref::Order> e;
            std::shared_ptr<Order> a;
            if (type == OrderType::Market) {
                e = std::make_shared<ref::Order>(id, side, quantity);
                a = std::make_shared<Order>(id, side, quantity);
            } else {
                const Price price = randomPrice();
                e = std::make_shared<ref::Order>(type, id, side, price, quantity);
                a = std::make_shared<Order>(type, id, side, price, quantity);
            }

            const Trades et = expected.AddOrder(e);
            const Trades at = actual.AddOrder(a);
            if (!SameTrades(et, at)) Fail(config, seed, op, "trades de AddOrder");
            if (e->GetRemainingQuantity() != a->GetRemainingQuantity() || e->GetPrice() != a->GetPrice() ||
                e->GetOrderType() != a->GetOrderType())
                Fail(config, seed, op, "état de l'ordre entrant après AddOrder");
            tradeCount += et.size();
            known.push_back(id);
        } else if (action < 100 - 12) {
            OrderId id = pick(50) == 0 ? nextId + 1000 : known[pick(known.size())];
            if (pick(4) != 0 && !known.empty()) {
                const std::size_t i = pick(known.size());
                id = known[i];
                known[i] = known.back();
                known.pop_back();
            }
            expected.CancelOrder(id);
            actual.CancelOrder(id);
        } else {
            const OrderId id = pick(50) == 0 ? nextId + 1000 : known[pick(known.size())];
            const Side side = pick(2) ? Side::Buy : Side::Sell;
            const Price price = randomPrice();
            const Quantity quantity = pick(200) == 0 ? 0 : randomQuantity();

            Trades et, at;
            bool eThrew = false, aThrew = false;
            try {
                et = expected.MatchOrder(ref::OrderModify(id, side, price, quantity));
            } catch (const std::invalid_argument &) {
                eThrew = true;
            }
            try {
                at = actual.MatchOrder(OrderModify(id, side, price, quantity));
            } catch (const std::invalid_argument &) {
                aThrew = true;
            }
            if (eThrew != aThrew) Fail(config, seed, op, "exception de MatchOrder");
            if (!SameTrades(et, at)) Fail(config, seed, op, "trades de MatchOrder");
            tradeCount += et.size();
        }

        if (expected.Size() != actual.Size()) Fail(config, seed, op, "Size()");
        if (op % 16 == 0 || op + 1 == operations) {
            const auto ei = expected.GetOrderInfos();
            const auto ai = actual.GetOrderInfos();
            if (!SameLevels(ei.GetBids(), ai.GetBids())) Fail(config, seed, op, "niveaux bid");
            if (!SameLevels(ei.GetAsks(), ai.GetAsks())) Fail(config, seed, op, "niveaux ask");
        }
        if (known.size() > 4096) known.erase(known.begin(), known.begin() + 2048);
    }
    return tradeCount;
}
} // namespace

int main(int argc, char **argv) {
    const std::size_t operations = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 150000;
    const std::uint64_t seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 20261112;

    ExchangeRules plain;
    ExchangeRules ticked;
    ticked.tickSize = 5;
    ticked.lotSize = 10;
    ticked.minQuantity = 10;
    ExchangeRules notional;
    notional.minNotional = 2000;
    notional.maxQuantity = 40;

    const Config configs[] = {
        {"serré", 100, 4, plain, 30},
        {"standard", 1000, 40, plain, 40},
        {"profond", 20000, 3000, plain, 12},
        {"pas=5 lot=10", 5000, 200, ticked, 35},
        {"notionnel", 300, 60, notional, 40},
        {"proche de zéro", 5, 12, plain, 40},
        {"fenêtre large", 150000, 90000, plain, 15},
    };

    std::uint64_t run = 0;
    for (const Config &config: configs) {
        const std::uint64_t trades = RunConfig(config, seed + run++, operations);
        std::cout << "[" << config.name << "] " << operations << " opérations identiques, " << trades << " trades\n";
    }
    std::cout << "Test différentiel réussi\n";
    return 0;
}
