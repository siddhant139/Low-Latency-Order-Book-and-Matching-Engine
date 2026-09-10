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
    std::cout << "=== Matching Engine Tests ===" << std::endl;

    // 1. No match rests order
    {
        OrderBook book("TEST");
        auto trades = book.addOrder(mkOrder(1, Side::BID, OrderType::LIMIT, 100.00, 10));
        check(trades.empty(), "no_match_rests: no trades");
        check(book.bestBid() == priceToTicks(100.00), "no_match_rests: bestBid == 10000");
        check(book.restingOrderCount() == 1, "no_match_rests: restingOrderCount == 1");
    }

    // 2. Simple full match
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::ASK, OrderType::LIMIT, 100.00, 10));
        auto trades = book.addOrder(mkOrder(2, Side::BID, OrderType::LIMIT, 100.00, 10));
        check(trades.size() == 1, "full_match: 1 trade");
        check(trades[0].quantity == 10, "full_match: qty == 10");
        check(trades[0].price == priceToTicks(100.00), "full_match: price == 10000");
        check(book.restingOrderCount() == 0, "full_match: book empty");
    }

    // 3. Partial fill incoming larger
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::ASK, OrderType::LIMIT, 100.00, 5));
        auto trades = book.addOrder(mkOrder(2, Side::BID, OrderType::LIMIT, 100.00, 10));
        check(trades.size() == 1, "partial_incoming_larger: 1 trade");
        check(trades[0].quantity == 5, "partial_incoming_larger: trade qty == 5");
        check(book.bestBid().has_value() && book.bestBid().value() == priceToTicks(100.00),
              "partial_incoming_larger: bestBid == 100.00");
        check(book.restingOrderCount() == 1, "partial_incoming_larger: restingOrderCount == 1");
    }

    // 4. Partial fill resting larger
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::ASK, OrderType::LIMIT, 100.00, 10));
        auto trades = book.addOrder(mkOrder(2, Side::BID, OrderType::LIMIT, 100.00, 4));
        check(trades.size() == 1, "partial_resting_larger: 1 trade");
        check(trades[0].quantity == 4, "partial_resting_larger: trade qty == 4");
        auto ask_depth = book.askDepth(1);
        check(!ask_depth.empty() && ask_depth[0].second == 6,
              "partial_resting_larger: ask remains qty == 6");
        check(book.bestAsk().has_value() && book.bestAsk().value() == priceToTicks(100.00),
              "partial_resting_larger: bestAsk == 100.00");
    }

    // 5. Price-time priority (FIFO at same price)
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::ASK, OrderType::LIMIT, 100.00, 5)); // ts=1
        book.addOrder(mkOrder(2, Side::ASK, OrderType::LIMIT, 100.00, 5)); // ts=2
        auto trades = book.addOrder(mkOrder(3, Side::BID, OrderType::LIMIT, 100.00, 5));
        check(trades.size() == 1, "price_time_priority: 1 trade");
        check(trades[0].sell_order_id == 1, "price_time_priority: matched id=1 first (FIFO)");
    }

    // 6. Price priority (better price matched first)
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::ASK, OrderType::LIMIT, 100.50, 5)); // worse ask
        book.addOrder(mkOrder(2, Side::ASK, OrderType::LIMIT, 100.00, 5)); // better ask
        auto trades = book.addOrder(mkOrder(3, Side::BID, OrderType::LIMIT, 101.00, 5));
        check(trades.size() == 1, "price_priority: 1 trade");
        check(trades[0].price == priceToTicks(100.00), "price_priority: matched at 100.00");
        check(trades[0].sell_order_id == 2, "price_priority: sell_order_id == 2 (better ask)");
    }

    // 7. No cross when prices don't meet
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::ASK, OrderType::LIMIT, 101.00, 5));
        auto trades = book.addOrder(mkOrder(2, Side::BID, OrderType::LIMIT, 100.00, 5));
        check(trades.empty(), "no_cross: 0 trades");
        check(book.bidLevels() == 1, "no_cross: bidLevels == 1");
        check(book.askLevels() == 1, "no_cross: askLevels == 1");
    }

    // 8. MARKET order consumes best regardless of price
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::ASK, OrderType::LIMIT, 100.00, 5));
        book.addOrder(mkOrder(2, Side::ASK, OrderType::LIMIT, 100.50, 5));
        auto trades = book.addOrder(mkOrder(3, Side::BID, OrderType::MARKET, 0.0, 8));
        check(trades.size() == 2, "market_consumes: 2 trades");
        Quantity total_filled = 0;
        for (const auto& t : trades) { total_filled += t.quantity; }
        check(total_filled == 8, "market_consumes: total filled == 8");
        check(trades[0].price == priceToTicks(100.00) && trades[0].quantity == 5,
              "market_consumes: first trade 5 @ 100.00");
        check(trades[1].price == priceToTicks(100.50) && trades[1].quantity == 3,
              "market_consumes: second trade 3 @ 100.50");
    }

    // 9. MARKET order remainder discarded
    {
        OrderBook book("TEST");
        auto trades = book.addOrder(mkOrder(1, Side::BID, OrderType::MARKET, 0.0, 10));
        check(trades.empty(), "market_discard: 0 trades on empty book");
        check(book.restingOrderCount() == 0, "market_discard: market never rests");
    }

    // 10. Multiple trades aggregate
    {
        OrderBook book("TEST");
        book.addOrder(mkOrder(1, Side::ASK, OrderType::LIMIT, 100.00, 3));
        book.addOrder(mkOrder(2, Side::ASK, OrderType::LIMIT, 100.00, 3));
        auto trades = book.addOrder(mkOrder(3, Side::BID, OrderType::LIMIT, 100.00, 6));
        check(trades.size() == 2, "multi_trades: 2 trades");
        Quantity total_qty = 0;
        for (const auto& t : trades) { total_qty += t.quantity; }
        check(total_qty == 6, "multi_trades: total qty == 6");
    }

    std::cout << "\nResults: " << tests_passed << " passed, " << tests_failed << " failed." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
