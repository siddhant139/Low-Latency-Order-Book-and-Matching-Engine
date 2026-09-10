#include <iostream>
#include <string>
#include <cmath>
#include "../src/order_book.h"
#include "../src/market_data.h"

int tests_passed = 0, tests_failed = 0;

void check(bool condition, const std::string& name) {
    if (condition) { std::cout << "  PASS: " << name << std::endl; tests_passed++; }
    else { std::cout << "  FAIL: " << name << std::endl; tests_failed++; }
}

using namespace orderbook;

static Order mkOrder(OrderId id, Side side, OrderType type, double price, Quantity qty) {
    Order o;
    o.id = id; o.side = side; o.type = type;
    o.price = priceToTicks(price); o.quantity = qty; o.remaining = qty;
    o.timestamp = id; // deterministic time priority by id
    return o;
}

int main() {
    std::cout << "=== Market Data Tests ===" << std::endl;

    // 1. Empty book
    {
        OrderBook book("TEST");
        check(!book.bestBid().has_value(), "empty: bestBid == nullopt");
        check(!book.bestAsk().has_value(), "empty: bestAsk == nullopt");
        check(!book.spread().has_value(), "empty: spread == nullopt");
        check(!book.midPrice().has_value(), "empty: midPrice == nullopt");
    }

    // 2. Best bid/ask
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 100.00, 10));
        book.addOrder(mkOrder(2, Side::ASK, OrderType::LIMIT, 101.00, 10));
        check(book.bestBid().has_value() && book.bestBid().value() == priceToTicks(100.00),
              "best_bid_ask: bestBid == 100.00");
        check(book.bestAsk().has_value() && book.bestAsk().value() == priceToTicks(101.00),
              "best_bid_ask: bestAsk == 101.00");
    }

    // 3. Spread
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 100.00, 10));
        book.addOrder(mkOrder(2, Side::ASK, OrderType::LIMIT, 101.00, 10));
        check(book.spread().has_value() && book.spread().value() == priceToTicks(1.00),
              "spread: spread == 100 ticks (1.00)");
    }

    // 4. Mid price
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 100.00, 10));
        book.addOrder(mkOrder(2, Side::ASK, OrderType::LIMIT, 101.00, 10));
        check(book.midPrice().has_value() && std::abs(book.midPrice().value() - 100.50) < 1e-9,
              "mid_price: midPrice ~= 100.50");
    }

    // 5. L2 depth ordering bids (highest first)
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 99.00, 10));
        book.addOrder(mkOrder(2, Side::BID, OrderType::LIMIT, 100.00, 20));
        book.addOrder(mkOrder(3, Side::BID, OrderType::LIMIT, 98.00, 30));
        auto depth = book.bidDepth(3);
        check(depth.size() == 3, "bid_depth_order: 3 levels");
        check(depth[0].first == priceToTicks(100.00), "bid_depth_order: first == 100.00 (best)");
        check(depth[1].first == priceToTicks(99.00), "bid_depth_order: second == 99.00");
        check(depth[2].first == priceToTicks(98.00), "bid_depth_order: third == 98.00");
    }

    // 6. L2 depth ordering asks (lowest first)
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::ASK, OrderType::LIMIT, 101.00, 10));
        book.addOrder(mkOrder(2, Side::ASK, OrderType::LIMIT, 100.50, 20));
        book.addOrder(mkOrder(3, Side::ASK, OrderType::LIMIT, 102.00, 5));
        auto depth = book.askDepth(3);
        check(depth.size() == 3, "ask_depth_order: 3 levels");
        check(depth[0].first == priceToTicks(100.50), "ask_depth_order: first == 100.50 (best)");
        check(depth[1].first == priceToTicks(101.00), "ask_depth_order: second == 101.00");
        check(depth[2].first == priceToTicks(102.00), "ask_depth_order: third == 102.00");
    }

    // 7. L2 aggregated quantity
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 100.00, 10));
        book.addOrder(mkOrder(2, Side::BID, OrderType::LIMIT, 100.00, 15));
        auto depth = book.bidDepth(1);
        check(!depth.empty() && depth[0].second == 25,
              "aggregated_qty: bidDepth[0].qty == 25");
    }

    // 8. L2 respects level limit
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 100.00, 10));
        book.addOrder(mkOrder(2, Side::BID, OrderType::LIMIT, 99.00, 10));
        book.addOrder(mkOrder(3, Side::BID, OrderType::LIMIT, 98.00, 10));
        book.addOrder(mkOrder(4, Side::BID, OrderType::LIMIT, 97.00, 10));
        book.addOrder(mkOrder(5, Side::BID, OrderType::LIMIT, 96.00, 10));
        auto depth = book.bidDepth(3);
        check(depth.size() == 3, "level_limit: bidDepth(3) returns exactly 3");
    }

    // 9. Snapshot
    {
        OrderBook book("AAPL");
        book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 100.00, 10));
        book.addOrder(mkOrder(2, Side::ASK, OrderType::LIMIT, 101.00, 10));
        L2Snapshot snap = MarketData::snapshot(book, 5);
        check(snap.symbol == "AAPL", "snapshot: symbol == AAPL");
        check(snap.best_bid.has_value() && snap.best_bid.value() == priceToTicks(100.00),
              "snapshot: best_bid == 100.00");
        check(snap.best_ask.has_value() && snap.best_ask.value() == priceToTicks(101.00),
              "snapshot: best_ask == 101.00");
    }

    // 10. formatL2 returns non-empty string containing the symbol
    {
        OrderBook book("MSFT");
        book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 100.00, 10));
        book.addOrder(mkOrder(2, Side::ASK, OrderType::LIMIT, 101.00, 10));
        L2Snapshot snap = MarketData::snapshot(book, 5);
        std::string formatted = MarketData::formatL2(snap);
        check(!formatted.empty(), "formatL2: non-empty string");
        check(formatted.find("MSFT") != std::string::npos, "formatL2: contains symbol MSFT");
    }

    std::cout << "\nResults: " << tests_passed << " passed, " << tests_failed << " failed." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
