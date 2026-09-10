#pragma once

#include <cstdint>
#include <string>
#include <deque>
#include <chrono>

namespace orderbook {

// Fixed-point price: store price as integer ticks to avoid floating-point errors.
// e.g. $105.25 with tick size $0.01 -> 10525 ticks.
// This is how real exchanges represent prices (never use double for prices).
using Ticks = int64_t;
using OrderId = uint64_t;
using Quantity = int32_t;
using Timestamp = uint64_t;  // nanoseconds

constexpr double TICK_SIZE = 0.01;  // $0.01 per tick

inline double ticksToPrice(Ticks t) { return static_cast<double>(t) * TICK_SIZE; }
inline Ticks priceToTicks(double p) { return static_cast<Ticks>(p / TICK_SIZE + 0.5); }

enum class Side { BID, ASK };

inline const char* sideToString(Side s) { return s == Side::BID ? "BID" : "ASK"; }

// Order types:
//   LIMIT  - match at or better than the limit price; rest any remainder in the book.
//   MARKET - match against best available prices regardless of price; discard remainder.
//   IOC    - Immediate-Or-Cancel: match what's available now at the limit price,
//            cancel (do not rest) any unfilled remainder.
//   FOK    - Fill-Or-Kill: fill the ENTIRE quantity immediately at the limit price,
//            or cancel the whole order (no partial fills).
enum class OrderType { LIMIT, MARKET, IOC, FOK };

inline const char* orderTypeToString(OrderType t) {
    switch (t) {
        case OrderType::LIMIT:  return "LIMIT";
        case OrderType::MARKET: return "MARKET";
        case OrderType::IOC:    return "IOC";
        case OrderType::FOK:    return "FOK";
    }
    return "UNKNOWN";
}

struct Order {
    OrderId id = 0;
    Side side = Side::BID;
    OrderType type = OrderType::LIMIT;
    Ticks price = 0;          // in ticks; ignored for MARKET orders
    Quantity quantity = 0;    // original quantity
    Quantity remaining = 0;   // unfilled quantity
    Timestamp timestamp = 0;  // arrival time (ns) for time priority
};

struct Trade {
    OrderId buy_order_id = 0;
    OrderId sell_order_id = 0;
    Ticks price = 0;          // execution price in ticks
    Quantity quantity = 0;    // filled quantity
    Timestamp timestamp = 0;
};

// A price level holds all resting orders at one price, in time-priority (FIFO) order.
struct PriceLevel {
    Ticks price = 0;
    Quantity total_quantity = 0;   // sum of remaining across orders (for O(1) L2 depth)
    std::deque<Order> orders;      // front = oldest (highest time priority)
};

inline Timestamp nowNanos() {
    return static_cast<Timestamp>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace orderbook
