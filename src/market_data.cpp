#include "market_data.h"
#include "order_book.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace orderbook {

L2Snapshot MarketData::snapshot(const OrderBook& book, int levels) {
    L2Snapshot snap;
    snap.symbol = book.symbol();
    snap.timestamp = nowNanos();
    snap.best_bid = book.bestBid();
    snap.best_ask = book.bestAsk();
    snap.spread = book.spread();
    snap.mid_price = book.midPrice();
    snap.bid_depth = book.bidDepth(levels);
    snap.ask_depth = book.askDepth(levels);
    return snap;
}

std::string MarketData::formatL2(const L2Snapshot& snap) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    oss << "=== " << snap.symbol << " L2 Book ===\n";

    // --- ASKS (descending price: highest at top, best ask at bottom) ---
    oss << "ASKS:\n";
    if (snap.ask_depth.empty()) {
        oss << "  (empty)\n";
    } else {
        // ask_depth is expected in ascending price order from the book;
        // display in descending order (highest price first).
        std::vector<std::pair<Ticks, Quantity>> asks(snap.ask_depth);
        std::sort(asks.begin(), asks.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [ticks, qty] : asks) {
            oss << "  " << ticksToPrice(ticks) << " x " << qty << "\n";
        }
    }

    oss << "----------------------\n";

    // --- BIDS (descending price: best bid at top) ---
    oss << "BIDS:\n";
    if (snap.bid_depth.empty()) {
        oss << "  (empty)\n";
    } else {
        // bid_depth is expected in descending price order from the book.
        std::vector<std::pair<Ticks, Quantity>> bids(snap.bid_depth);
        std::sort(bids.begin(), bids.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [ticks, qty] : bids) {
            oss << "  " << ticksToPrice(ticks) << " x " << qty << "\n";
        }
    }

    // --- Spread and mid-price ---
    if (snap.spread.has_value() && snap.mid_price.has_value()) {
        oss << "Spread: " << ticksToPrice(snap.spread.value())
            << "  Mid: " << snap.mid_price.value() << "\n";
    } else {
        oss << "Spread: N/A  Mid: N/A\n";
    }

    return oss.str();
}

} // namespace orderbook
