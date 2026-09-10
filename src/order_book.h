#pragma once

#include "types.h"
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include <optional>

namespace orderbook {

// Limit order book with price-time priority matching.
// Bids sorted descending (highest first), asks ascending (lowest first).
class OrderBook {
public:
    // `expected_orders` optionally pre-reserves the internal order index to
    // avoid hash-table rehashing under load (which causes latency-tail spikes).
    // Pass 0 (default) to skip reservation.
    explicit OrderBook(const std::string& symbol, size_t expected_orders = 0);

    // Add an order. Returns the trades generated (empty if it just rests).
    // For LIMIT orders: match against the opposite side, rest the remainder.
    // For MARKET orders: match as much as possible, discard any remainder.
    std::vector<Trade> addOrder(Order order);

    // Cancel a resting order by id. Returns true if found and removed.
    bool cancelOrder(OrderId id);

    // Modify a resting order's quantity and/or price.
    // If price changes, the order loses time priority (cancel + re-add).
    // If only quantity decreases, time priority is retained.
    // Returns trades generated (if the re-added order crosses the book).
    std::vector<Trade> modifyOrder(OrderId id, Quantity new_quantity, Ticks new_price);

    // --- Market data (L1) ---
    std::optional<Ticks> bestBid() const;
    std::optional<Ticks> bestAsk() const;
    std::optional<Ticks> spread() const;      // best_ask - best_bid
    std::optional<double> midPrice() const;    // (best_bid + best_ask) / 2 in dollars

    // --- Market data (L2) ---
    // Returns up to `levels` (price, total_qty) pairs from the top of each side.
    std::vector<std::pair<Ticks, Quantity>> bidDepth(int levels) const;
    std::vector<std::pair<Ticks, Quantity>> askDepth(int levels) const;

    // --- Stats ---
    const std::string& symbol() const { return symbol_; }
    uint64_t totalOrders() const { return total_orders_; }
    uint64_t totalTrades() const { return total_trades_; }
    uint64_t totalCancels() const { return total_cancels_; }
    size_t restingOrderCount() const { return order_index_.size(); }
    size_t bidLevels() const { return bids_.size(); }
    size_t askLevels() const { return asks_.size(); }

private:
    std::string symbol_;

    // Bids: highest price first (std::greater). Asks: lowest price first (default).
    std::map<Ticks, PriceLevel, std::greater<Ticks>> bids_;
    std::map<Ticks, PriceLevel, std::less<Ticks>> asks_;

    // Locate a resting order for O(1) cancel/modify.
    struct OrderLocation { Side side; Ticks price; };
    std::unordered_map<OrderId, OrderLocation> order_index_;

    uint64_t total_orders_ = 0;
    uint64_t total_trades_ = 0;
    uint64_t total_cancels_ = 0;

    // Match an incoming order against the opposite side. Mutates order.remaining.
    std::vector<Trade> match(Order& incoming);
    // Rest the (partially filled) order into the book.
    void rest(const Order& order);
    // FOK feasibility check: can the entire order be filled immediately at
    // acceptable prices? Walks the opposite side WITHOUT mutating the book.
    bool canFullyFill(const Order& order) const;

    template <typename BookSide>
    std::vector<std::pair<Ticks, Quantity>> depth(const BookSide& side, int levels) const;
};

} // namespace orderbook
