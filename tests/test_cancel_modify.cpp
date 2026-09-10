#include <iostream>
#include <string>
#include "../src/order_book.h"

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
    std::cout << "=== Cancel / Modify Tests ===" << std::endl;

    // 1. Cancel existing
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 100.00, 10));
        bool ok = book.cancelOrder(1);
        check(ok, "cancel_existing: returns true");
        check(book.restingOrderCount() == 0, "cancel_existing: restingOrderCount == 0");
        check(!book.bestBid().has_value(), "cancel_existing: bestBid == nullopt");
    }

    // 2. Cancel non-existent
    {
        OrderBook book("TEST");
        bool ok = book.cancelOrder(999);
        check(!ok, "cancel_nonexistent: returns false");
    }

    // 3. Cancel removes price level
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 100.00, 10));
        book.cancelOrder(1);
        check(book.bidLevels() == 0, "cancel_removes_level: bidLevels == 0");
    }

    // 4. Cancel one of two at same level
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 100.00, 10));
        book.addOrder(mkOrder(2, Side::BID, OrderType::LIMIT, 100.00, 15));
        book.cancelOrder(1);
        check(book.bidLevels() == 1, "cancel_one_of_two: level still exists");
        auto depth = book.bidDepth(1);
        check(!depth.empty() && depth[0].second == 15,
              "cancel_one_of_two: depth qty == 15 (id2 qty)");
        check(book.restingOrderCount() == 1, "cancel_one_of_two: restingOrderCount == 1");
    }

    // 5. Modify reduce quantity retains level
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 100.00, 10));
        auto trades = book.modifyOrder(1, 5, priceToTicks(100.00));
        check(trades.empty(), "modify_reduce_qty: no trades");
        auto depth = book.bidDepth(1);
        check(!depth.empty() && depth[0].second == 5,
              "modify_reduce_qty: depth qty == 5");
        check(book.restingOrderCount() == 1, "modify_reduce_qty: restingOrderCount == 1");
    }

    // 6. Modify price moves order
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 100.00, 10));
        book.modifyOrder(1, 10, priceToTicks(101.00));
        check(book.bestBid().has_value() && book.bestBid().value() == priceToTicks(101.00),
              "modify_price: bestBid == 101.00");
    }

    // 7. Modify to crossing price generates trade
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::ASK, OrderType::LIMIT, 100.00, 10));
        book.addOrder(mkOrder(2, Side::BID, OrderType::LIMIT, 99.00, 10)); // rests, no cross
        auto trades = book.modifyOrder(2, 10, priceToTicks(100.00));
        check(trades.size() == 1, "modify_cross: 1 trade generated");
        check(trades[0].quantity == 10, "modify_cross: trade qty == 10");
    }

    // 8. Modify non-existent returns empty
    {
        OrderBook book("TEST");
        auto trades = book.modifyOrder(999, 5, priceToTicks(100.00));
        check(trades.empty(), "modify_nonexistent: returns empty");
    }

    // 9. totalCancels increments
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 100.00, 10));
        book.addOrder(mkOrder(2, Side::BID, OrderType::LIMIT, 101.00, 10));
        book.cancelOrder(1);
        book.cancelOrder(2);
        check(book.totalCancels() == 2, "total_cancels: totalCancels == 2");
    }

    std::cout << "\nResults: " << tests_passed << " passed, " << tests_failed << " failed." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
