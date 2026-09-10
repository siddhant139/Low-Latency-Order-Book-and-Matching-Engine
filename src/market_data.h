#pragma once

#include "types.h"
#include <vector>
#include <string>
#include <optional>

namespace orderbook {

class OrderBook;

// A point-in-time L2 snapshot of the book.
struct L2Snapshot {
    std::string symbol;
    Timestamp timestamp = 0;
    std::optional<Ticks> best_bid;
    std::optional<Ticks> best_ask;
    std::optional<Ticks> spread;
    std::optional<double> mid_price;
    std::vector<std::pair<Ticks, Quantity>> bid_depth;
    std::vector<std::pair<Ticks, Quantity>> ask_depth;
};

class MarketData {
public:
    // Build an L2 snapshot from the current book state (top `levels` per side).
    static L2Snapshot snapshot(const OrderBook& book, int levels = 5);

    // Human-readable rendering of the book ladder.
    static std::string formatL2(const L2Snapshot& snap);
};

} // namespace orderbook
