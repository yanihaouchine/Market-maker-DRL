#pragma once

#include <utility>
#include <vector>
#include "Core/Types.h"

struct LevelInfo {
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderbookLevelInfos {
public:
    OrderbookLevelInfos(const LevelInfos &bids, const LevelInfos &asks)
        : bids_{bids},
          asks_{asks} {
    }

    OrderbookLevelInfos(LevelInfos &&bids, LevelInfos &&asks)
        : bids_{std::move(bids)},
          asks_{std::move(asks)} {
    }

    const LevelInfos &GetBids() const { return bids_; }
    const LevelInfos &GetAsks() const { return asks_; }

private:
    LevelInfos bids_;
    LevelInfos asks_;
};
