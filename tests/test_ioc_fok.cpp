// Tests for IOC (Immediate-Or-Cancel) and FOK (Fill-Or-Kill) order types.
#include <iostream>
#include <string>

#include "../src/order_book.h"

using namespace orderbook;

int tests_passed = 0, tests_failed = 0;
void check(bool condition, const std::string& name) {
    if (condition) { std::cout << "  PASS: " << name << std::endl; tests_passed++; }
    else { std::cout << "  FAIL: " << name << std::endl; tests_failed++; }
}

static Order mk(OrderId id, Side side, OrderType type, double price, Quantity qty) {
    Order o;
    o.id = id; o.side = side; o.type = type;
    o.price = priceToTicks(price); o.quantity = qty; o.remaining = qty;
    o.timestamp = id;
    return o;
}

int main() {
    std::cout << "=== IOC / FOK Order Type Tests ===\n";

    // ---------------- IOC ----------------
    // IOC fills what it can at the limit price, cancels the rest (never rests).
    {
        std::cout << "\n[IOC: partial fill then cancel remainder]\n";
        OrderBook book("T");
        book.addOrder(mk(1, Side::ASK, OrderType::LIMIT, 100.00, 30));  // only 30 available
        auto trades = book.addOrder(mk(2, Side::BID, OrderType::IOC, 100.00, 100));  // want 100
        Quantity filled = 0; for (auto& t : trades) filled += t.quantity;
        check(filled == 30, "IOC filled the 30 available");
        check(book.restingOrderCount() == 0, "IOC did NOT rest the unfilled 70 (book empty)");
        check(book.bestBid() == std::nullopt, "no resting bid left behind by IOC");
    }
    {
        std::cout << "\n[IOC: respects limit price]\n";
        OrderBook book("T");
        book.addOrder(mk(1, Side::ASK, OrderType::LIMIT, 101.00, 50));  // priced ABOVE the IOC limit
        auto trades = book.addOrder(mk(2, Side::BID, OrderType::IOC, 100.00, 50));  // limit 100 < 101
        check(trades.empty(), "IOC does not cross a worse-priced ask");
        check(book.restingOrderCount() == 1, "the resting ask is untouched");
    }
    {
        std::cout << "\n[IOC: full fill when liquidity available]\n";
        OrderBook book("T");
        book.addOrder(mk(1, Side::ASK, OrderType::LIMIT, 100.00, 100));
        auto trades = book.addOrder(mk(2, Side::BID, OrderType::IOC, 100.00, 100));
        Quantity filled = 0; for (auto& t : trades) filled += t.quantity;
        check(filled == 100, "IOC fully filled against sufficient liquidity");
        check(book.restingOrderCount() == 0, "book fully cleared");
    }

    // ---------------- FOK ----------------
    // FOK fills the ENTIRE quantity immediately, or cancels the whole order.
    {
        std::cout << "\n[FOK: killed when not enough liquidity]\n";
        OrderBook book("T");
        book.addOrder(mk(1, Side::ASK, OrderType::LIMIT, 100.00, 30));  // only 30 available
        auto trades = book.addOrder(mk(2, Side::BID, OrderType::FOK, 100.00, 100));  // need 100
        check(trades.empty(), "FOK generated no trades (killed)");
        check(book.restingOrderCount() == 1, "resting ask untouched — FOK made NO partial fill");
        // The 30 ask must still be fully there.
        auto depth = book.askDepth(1);
        check(!depth.empty() && depth[0].second == 30, "ask quantity unchanged at 30");
    }
    {
        std::cout << "\n[FOK: fully filled when enough liquidity]\n";
        OrderBook book("T");
        book.addOrder(mk(1, Side::ASK, OrderType::LIMIT, 100.00, 60));
        book.addOrder(mk(2, Side::ASK, OrderType::LIMIT, 100.00, 60));  // 120 total at 100
        auto trades = book.addOrder(mk(3, Side::BID, OrderType::FOK, 100.00, 100));  // need 100
        Quantity filled = 0; for (auto& t : trades) filled += t.quantity;
        check(filled == 100, "FOK fully filled 100 across two asks");
        check(book.restingOrderCount() == 1, "20 leftover ask liquidity remains resting");
    }
    {
        std::cout << "\n[FOK: killed when price doesn't reach]\n";
        OrderBook book("T");
        book.addOrder(mk(1, Side::ASK, OrderType::LIMIT, 101.00, 100));  // above FOK limit
        auto trades = book.addOrder(mk(2, Side::BID, OrderType::FOK, 100.00, 100));
        check(trades.empty(), "FOK killed — no liquidity at or below limit price");
        check(book.restingOrderCount() == 1, "resting ask untouched");
    }
    {
        std::cout << "\n[FOK: never rests]\n";
        OrderBook book("T");
        book.addOrder(mk(1, Side::ASK, OrderType::LIMIT, 100.00, 100));
        book.addOrder(mk(2, Side::BID, OrderType::FOK, 100.00, 100));  // fully fills
        check(book.restingOrderCount() == 0, "FOK left nothing resting after full fill");
    }

    std::cout << "\n=== Results: " << tests_passed << " passed, " << tests_failed << " failed ===\n";
    return tests_failed > 0 ? 1 : 0;
}
