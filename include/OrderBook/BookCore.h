#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <limits>
#include <vector>

#include "Core/OrderType.h"
#include "Core/Types.h"
#include "OrderBook/ExchangeRules.h"
#include "OrderBook/IdTable.h"
#include "OrderBook/LevelInfo.h"
#include "OrderBook/OrderPool.h"
#include "OrderBook/PriceLevels.h"
#include "OrderBook/Trade.h"

namespace ob {
// Cœur du moteur de matching. Aucune allocation sur le chemin chaud : les ordres vivent dans un
// pool contigu, l'index des identifiants est une table à adressage ouvert, et les trades sont
// ajoutés au vecteur fourni par l'appelant (qui le réutilise d'un appel à l'autre).
//
// `Levels` est la structure des niveaux de prix d'un côté (voir PriceLevels.h), `Ids` la table
// des identifiants ; les deux sont des paramètres pour pouvoir les comparer au banc de mesure.
template<class Levels, class Ids = DirectIdTable<>>
class BookCore {
public:
    BookCore() : lastDayReset_(std::time(nullptr)) {
    }

    // Ajoute un ordre et renvoie sa quantité non exécutée (qu'elle soit inscrite au carnet,
    // annulée ou rejetée). Les trades sont ajoutés à la fin de `trades`.
    Quantity Add(OrderType type, OrderId id, Side side, Price price, Quantity quantity, Trades &trades) {
        CheckDay();

        const unsigned own = static_cast<unsigned>(side);
        const unsigned opposite = own ^ 1u;

        if (type == OrderType::Market) {
            if (levels_[opposite].Empty()) return quantity;
            price = side == Side::Buy ? std::numeric_limits<Price>::max() : std::numeric_limits<Price>::min();
            type = OrderType::GoodTillCancel;
        }

        if (!Validate(id, price, quantity)) [[unlikely]] {
            return quantity;
        }

        // Clé limite côté opposé : un niveau adverse est exécutable si sa clé est <= limit.
        const std::uint32_t limit = PriceToKey(price, kSideMask[opposite]);

        if (type == OrderType::ImmediateOrCancel) {
            const Level *best = levels_[opposite].Best();
            if (!best || best->key > limit) return quantity;
        } else if (type == OrderType::FillOrKill) {
            if (!CanFill(opposite, limit, quantity)) return quantity;
            return Take(side, id, price, quantity, limit, trades);
        }

        if (crossed_) [[unlikely]] {
            return AddToCrossedBook(type, id, side, price, quantity, trades);
        }

        const Quantity remaining = Take(side, id, price, quantity, limit, trades);
        if (remaining != 0 && type != OrderType::ImmediateOrCancel) {
            Rest(type, id, side, price, remaining);
        }
        return remaining;
    }

    bool Cancel(OrderId id) {
        const std::uint32_t index = ids_.Find(id);
        if (index == kNil) return false;
        Remove(index);
        return true;
    }

    // Annule puis réinsère l'ordre avec son type d'origine (il perd sa priorité temporelle).
    // Renvoie false si l'identifiant est inconnu. Une quantité nulle revient à une annulation.
    bool Modify(OrderId id, Side side, Price price, Quantity quantity, Trades &trades) {
        CheckDay();
        const std::uint32_t index = ids_.Find(id);
        if (index == kNil) return false;
        const OrderType type = pool_[index].type;
        Remove(index);
        if (quantity != 0) Add(type, id, side, price, quantity, trades);
        return true;
    }

    // Inscrit un ordre au carnet sans validation ni matching (chargement d'un instantané).
    void InsertResting(OrderType type, OrderId id, Side side, Price price, Quantity quantity) {
        Rest(type, id, side, price, quantity);
        const Level *bid = levels_[0].Best();
        const Level *ask = levels_[1].Best();
        crossed_ = bid && ask && KeyToPrice(bid->key, kSideMask[0]) >= KeyToPrice(ask->key, kSideMask[1]);
    }

    void Clear() {
        levels_[0].Clear();
        levels_[1].Clear();
        ids_.Clear();
        pool_.Clear();
        crossed_ = false;
    }

    void Reserve(std::size_t orders) {
        pool_.Reserve(orders);
        ids_.Reserve(orders);
    }

    std::size_t Size() const { return ids_.Size(); }
    bool Contains(OrderId id) const { return ids_.Find(id) != kNil; }
    bool SideEmpty(Side side) const { return levels_[static_cast<unsigned>(side)].Empty(); }
    void PrefetchId(OrderId id) const { ids_.Prefetch(id); }

    // Niveaux agrégés d'un côté, du meilleur au moins bon. O(nombre de niveaux).
    void AppendLevels(Side side, LevelInfos &out) const {
        const unsigned s = static_cast<unsigned>(side);
        out.reserve(out.size() + levels_[s].Size());
        levels_[s].ForEach([&out, s](const Level &level) {
            out.push_back(LevelInfo{KeyToPrice(level.key, kSideMask[s]), static_cast<Quantity>(level.quantity)});
            return true;
        });
    }

    void SetExchangeRules(const ExchangeRules &rules) {
        rules_ = rules;
        unitRules_ = rules.tickSize == 1 && rules.lotSize == 1;
    }

    const ExchangeRules &GetExchangeRules() const { return rules_; }

    void SetDayResetTime(int hour, int minute) {
        if (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
            dayResetHour_ = hour;
            dayResetMinute_ = minute;
            nextDayCheck_ = 0;
        }
    }

    // Annule les ordres GoodForDay quand l'heure de remise à zéro est franchie. Sur le chemin
    // chaud : une lecture d'horloge grossière (vDSO) et une comparaison.
    void CheckDay() {
        const std::time_t now = std::time(nullptr);
        if (now < nextDayCheck_) [[likely]] {
            return;
        }
        ResetDay(now);
    }

private:
    bool Validate(OrderId id, Price price, Quantity quantity) const {
        if (quantity == 0) return false;
        if (ids_.Find(id) != kNil) return false;

        const bool extreme = price == std::numeric_limits<Price>::max() ||
                             price == std::numeric_limits<Price>::min();
        if (unitRules_) [[likely]] {
            // Pas de cotation et lot unitaires : aucune division sur le chemin chaud.
            if (!extreme && price <= 0) return false;
            if (quantity < rules_.minQuantity || quantity > rules_.maxQuantity) return false;
        } else {
            if (!extreme && !rules_.IsValidPrice(price)) return false;
            if (!rules_.IsValidQuantity(quantity)) return false;
        }
        return extreme || rules_.IsValidNotional(price, quantity);
    }

    // Vrai si le côté `opposite` offre au moins `quantity` à des clés <= limit.
    bool CanFill(unsigned opposite, std::uint32_t limit, Quantity quantity) const {
        std::uint64_t available = 0;
        levels_[opposite].ForEach([&available, limit, quantity](const Level &level) {
            if (level.key > limit) return false;
            available += level.quantity;
            return available < quantity;
        });
        return available >= quantity;
    }

    // Exécute l'ordre entrant contre le côté opposé tant que les prix se croisent.
    // Renvoie la quantité restante.
    Quantity Take(Side side, OrderId id, Price price, Quantity remaining, std::uint32_t limit, Trades &trades) {
        Levels &book = levels_[static_cast<unsigned>(side) ^ 1u];

        while (remaining != 0) {
            Level *level = book.Best();
            if (!level || level->key > limit) break;

            std::uint32_t index = level->head;
            do {
                OrderNode &resting = pool_[index];
                const Quantity quantity = remaining < resting.quantity ? remaining : resting.quantity;

                if (side == Side::Buy) {
                    trades.emplace_back(TradeInfo{id, price, quantity},
                                        TradeInfo{resting.id, resting.price, quantity});
                } else {
                    trades.emplace_back(TradeInfo{resting.id, resting.price, quantity},
                                        TradeInfo{id, price, quantity});
                }

                remaining -= quantity;
                resting.quantity -= quantity;
                level->quantity -= quantity;

                if (resting.quantity != 0) break;
                const std::uint32_t next = resting.next;
                ids_.Erase(resting.id);
                pool_.Free(index);
                index = next;
            } while (remaining != 0 && index != kNil);

            if (index == kNil) {
                book.Erase(level);
            } else {
                level->head = index;
                pool_[index].prev = kNil;
            }
        }
        return remaining;
    }

    void Rest(OrderType type, OrderId id, Side side, Price price, Quantity quantity) {
        const unsigned s = static_cast<unsigned>(side);
        const std::uint32_t index = pool_.Allocate();
        Level *level = levels_[s].FindOrCreate(PriceToKey(price, kSideMask[s]));

        OrderNode &node = pool_[index];
        node.id = id;
        node.price = price;
        node.quantity = quantity;
        node.prev = level->tail;
        node.next = kNil;
        node.type = type;
        node.side = side;

        if (level->tail == kNil) {
            level->head = index;
        } else {
            pool_[level->tail].next = index;
        }
        level->tail = index;
        level->quantity += quantity;

        ids_.Insert(id, index);
    }

    void Remove(std::uint32_t index) {
        const OrderNode &node = pool_[index];
        const unsigned s = static_cast<unsigned>(node.side);
        Level *level = levels_[s].Find(PriceToKey(node.price, kSideMask[s]));

        if (node.prev == kNil) {
            level->head = node.next;
        } else {
            pool_[node.prev].next = node.next;
        }
        if (node.next == kNil) {
            level->tail = node.prev;
        } else {
            pool_[node.next].prev = node.prev;
        }
        level->quantity -= node.quantity;
        if (level->head == kNil) levels_[s].Erase(level);

        ids_.Erase(node.id);
        pool_.Free(index);
    }

    // Chemin rare : après un instantané croisé, le carnet contient des ordres déjà exécutables
    // entre eux. On reproduit alors l'algorithme général : inscrire l'ordre, puis apparier les
    // têtes des deux meilleurs niveaux tant qu'ils se croisent.
    [[gnu::noinline, gnu::cold]]
    Quantity AddToCrossedBook(OrderType type, OrderId id, Side side, Price price, Quantity quantity,
                              Trades &trades) {
        Rest(type, id, side, price, quantity);

        while (true) {
            Level *bids = levels_[0].Best();
            Level *asks = levels_[1].Best();
            if (!bids || !asks) break;
            if (KeyToPrice(bids->key, kSideMask[0]) < KeyToPrice(asks->key, kSideMask[1])) break;

            while (bids->head != kNil && asks->head != kNil) {
                const std::uint32_t bidIndex = bids->head;
                const std::uint32_t askIndex = asks->head;
                OrderNode &bid = pool_[bidIndex];
                OrderNode &ask = pool_[askIndex];
                const Quantity traded = bid.quantity < ask.quantity ? bid.quantity : ask.quantity;

                trades.emplace_back(TradeInfo{bid.id, bid.price, traded}, TradeInfo{ask.id, ask.price, traded});

                bid.quantity -= traded;
                ask.quantity -= traded;
                bids->quantity -= traded;
                asks->quantity -= traded;

                if (bid.quantity == 0) PopHead(bids, bidIndex);
                if (ask.quantity == 0) PopHead(asks, askIndex);
            }

            // Effacer d'abord le niveau ask : les deux côtés sont des conteneurs distincts,
            // l'effacement de l'un n'invalide pas le pointeur vers l'autre.
            if (asks->head == kNil) levels_[1].Erase(asks);
            if (bids->head == kNil) levels_[0].Erase(bids);
        }
        crossed_ = false;

        const std::uint32_t index = ids_.Find(id);
        if (index == kNil) return 0;
        const Quantity remaining = pool_[index].quantity;
        if (type == OrderType::ImmediateOrCancel) Remove(index);
        return remaining;
    }

    void PopHead(Level *level, std::uint32_t index) {
        const std::uint32_t next = pool_[index].next;
        level->head = next;
        if (next == kNil) {
            level->tail = kNil;
        } else {
            pool_[next].prev = kNil;
        }
        ids_.Erase(pool_[index].id);
        pool_.Free(index);
    }

    [[gnu::noinline, gnu::cold]] void ResetDay(std::time_t now) {
        std::tm nowTm = *std::localtime(&now);

        std::tm resetTm = nowTm;
        resetTm.tm_hour = dayResetHour_;
        resetTm.tm_min = dayResetMinute_;
        resetTm.tm_sec = 0;
        const std::time_t todayReset = std::mktime(&resetTm);

        if (lastDayReset_ < todayReset && now >= todayReset) {
            CancelGoodForDayOrders();
            lastDayReset_ = now;
        }

        if (now < todayReset) {
            nextDayCheck_ = todayReset;
        } else {
            std::tm tomorrowTm = nowTm;
            tomorrowTm.tm_mday += 1; // mktime normalise le changement de mois
            tomorrowTm.tm_hour = dayResetHour_;
            tomorrowTm.tm_min = dayResetMinute_;
            tomorrowTm.tm_sec = 0;
            tomorrowTm.tm_isdst = -1;
            nextDayCheck_ = std::mktime(&tomorrowTm);
        }
    }

    void CancelGoodForDayOrders() {
        std::vector<std::uint32_t> expired;
        for (const Levels &side: levels_) {
            side.ForEach([this, &expired](const Level &level) {
                for (std::uint32_t i = level.head; i != kNil; i = pool_[i].next) {
                    if (pool_[i].type == OrderType::GoodForDay) expired.push_back(i);
                }
                return true;
            });
        }
        for (const std::uint32_t index: expired) Remove(index);
    }

    OrderPool pool_;
    Ids ids_;
    Levels levels_[2]; // indexé par Side : 0 = achats, 1 = ventes

    ExchangeRules rules_;
    bool unitRules_ = true;
    bool crossed_ = false;

    std::time_t nextDayCheck_ = 0;
    std::time_t lastDayReset_;
    int dayResetHour_ = 15;
    int dayResetMinute_ = 59;
};
} // namespace ob
