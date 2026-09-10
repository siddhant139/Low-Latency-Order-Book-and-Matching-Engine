// Per-operation benchmark: measures INSERT, MATCH, and CANCEL rates separately.
//
// Reporting a single blended "orders/sec" hides which operation is the
// bottleneck. Exchanges care about each independently, so we isolate them:
//   - insert: add resting limit orders that never cross (pure book insertion)
//   - match:  send crossing orders that each generate a trade (pure matching)
//   - cancel: remove previously-inserted resting orders (pure cancellation)

#include "../src/order_book.h"
#include "../src/types.h"

#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <cstdint>

using namespace orderbook;

static double rate(uint64_t n, std::chrono::nanoseconds elapsed) {
    double ns = static_cast<double>(elapsed.count());
    return ns > 0 ? (static_cast<double>(n) * 1e9 / ns) : 0.0;
}

int main(int argc, char* argv[]) {
    int n = 1000000;
    if (argc > 1) n = std::stoi(argv[1]);

    using clock = std::chrono::high_resolution_clock;

    // ---------------------------------------------------------------
    // INSERT: add N bids clustered across a realistic price band (no asks
    // present, so nothing crosses). Real books have hundreds/thousands of
    // levels, not one-per-order, so orders mostly append to existing levels.
    // ---------------------------------------------------------------
    const int LEVELS = 1000;  // realistic number of distinct price levels
    double insert_rate = 0.0;
    {
        OrderBook book("INS", static_cast<size_t>(n));
        std::vector<Order> orders;
        orders.reserve(n);
        for (int i = 0; i < n; ++i) {
            Order o;
            o.id = static_cast<OrderId>(i + 1);
            o.side = Side::BID;
            o.type = OrderType::LIMIT;
            o.price = 9000 + (i % LEVELS);  // clustered across LEVELS price levels; no asks -> never crosses
            o.quantity = 10;
            o.remaining = 10;
            o.timestamp = static_cast<Timestamp>(i + 1);
            orders.push_back(o);
        }
        auto t0 = clock::now();
        for (auto& o : orders) book.addOrder(o);
        auto t1 = clock::now();
        insert_rate = rate(n, t1 - t0);
        std::cout << "Inserted " << book.restingOrderCount() << " resting orders across "
                  << book.bidLevels() << " levels (0 trades expected)\n";
    }

    // ---------------------------------------------------------------
    // MATCH: pre-load N resting asks (qty 1 each at the same price),
    // then time N incoming bids that each fully match one ask -> pure
    // matching path (one trade per incoming order).
    // ---------------------------------------------------------------
    double match_rate = 0.0;
    {
        OrderBook book("MTC", static_cast<size_t>(n) * 2);
        const Ticks price = 10000;
        for (int i = 0; i < n; ++i) {
            Order ask;
            ask.id = static_cast<OrderId>(i + 1);
            ask.side = Side::ASK;
            ask.type = OrderType::LIMIT;
            ask.price = price;
            ask.quantity = 1;
            ask.remaining = 1;
            ask.timestamp = static_cast<Timestamp>(i + 1);
            book.addOrder(ask);
        }

        std::vector<Order> bids;
        bids.reserve(n);
        for (int i = 0; i < n; ++i) {
            Order bid;
            bid.id = static_cast<OrderId>(n + i + 1);
            bid.side = Side::BID;
            bid.type = OrderType::LIMIT;
            bid.price = price;              // crosses the resting asks
            bid.quantity = 1;
            bid.remaining = 1;
            bid.timestamp = static_cast<Timestamp>(n + i + 1);
            bids.push_back(bid);
        }

        auto t0 = clock::now();
        uint64_t trades = 0;
        for (auto& b : bids) trades += book.addOrder(b).size();
        auto t1 = clock::now();
        match_rate = rate(n, t1 - t0);
        std::cout << "Matched " << trades << " trades\n";
    }

    // ---------------------------------------------------------------
    // CANCEL: insert N resting orders (clustered across a realistic band),
    // then time cancelling all of them.
    // ---------------------------------------------------------------
    double cancel_rate = 0.0;
    {
        OrderBook book("CXL", static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            Order o;
            o.id = static_cast<OrderId>(i + 1);
            o.side = Side::BID;
            o.type = OrderType::LIMIT;
            o.price = 9000 + (i % LEVELS);
            o.quantity = 10;
            o.remaining = 10;
            o.timestamp = static_cast<Timestamp>(i + 1);
            book.addOrder(o);
        }

        auto t0 = clock::now();
        uint64_t cancelled = 0;
        for (int i = 0; i < n; ++i) {
            if (book.cancelOrder(static_cast<OrderId>(i + 1))) ++cancelled;
        }
        auto t1 = clock::now();
        cancel_rate = rate(n, t1 - t0);
        std::cout << "Cancelled " << cancelled << " orders\n";
    }

    std::cout << std::fixed << std::setprecision(0);
    std::cout << "\n=== Per-Operation Throughput (" << n << " ops each) ===\n";
    std::cout << "  Insert:  " << insert_rate << " ops/sec\n";
    std::cout << "  Match:   " << match_rate  << " ops/sec\n";
    std::cout << "  Cancel:  " << cancel_rate << " ops/sec\n";

    return 0;
}
