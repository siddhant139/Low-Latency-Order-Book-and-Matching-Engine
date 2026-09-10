#include "order_book.h"
#include <algorithm>

namespace orderbook {

OrderBook::OrderBook(const std::string& symbol, size_t expected_orders) : symbol_(symbol) {
    // Pre-reserve the order index so it never rehashes under load. Rehashing a
    // large hash map is the primary cause of multi-millisecond latency-tail
    // spikes in the matching hot path.
    if (expected_orders > 0) {
        order_index_.reserve(expected_orders);
    }
}

std::vector<Trade> OrderBook::addOrder(Order order) {
    ++total_orders_;
    if (order.timestamp == 0) order.timestamp = nowNanos();
    order.remaining = order.quantity;

    // FOK (Fill-Or-Kill): only proceed if the ENTIRE quantity can be filled
    // immediately. Otherwise kill the order with no state change and no trades.
    if (order.type == OrderType::FOK && !canFullyFill(order)) {
        return {};
    }

    std::vector<Trade> trades = match(order);

    // Only LIMIT orders rest their remainder in the book.
    // MARKET, IOC, and FOK never rest: any unfilled quantity is discarded/cancelled.
    if (order.type == OrderType::LIMIT && order.remaining > 0) {
        rest(order);
    }
    return trades;
}

// Walks the opposite side counting fillable quantity at acceptable prices,
// without mutating the book. Used for FOK feasibility.
bool OrderBook::canFullyFill(const Order& order) const {
    Quantity need = order.remaining;
    const bool respectPrice = (order.type != OrderType::MARKET);

    if (order.side == Side::BID) {
        for (const auto& [price, level] : asks_) {
            if (respectPrice && price > order.price) break;  // too expensive
            need -= std::min(need, level.total_quantity);
            if (need <= 0) return true;
        }
    } else {
        for (const auto& [price, level] : bids_) {
            if (respectPrice && price < order.price) break;  // too cheap
            need -= std::min(need, level.total_quantity);
            if (need <= 0) return true;
        }
    }
    return need <= 0;
}

std::vector<Trade> OrderBook::match(Order& incoming) {
    std::vector<Trade> trades;

    auto tryMatch = [&](auto& oppositeSide, auto priceOk) {
        while (incoming.remaining > 0 && !oppositeSide.empty()) {
            auto it = oppositeSide.begin();
            Ticks level_price = it->first;
            // All order types respect the limit price EXCEPT MARKET, which
            // sweeps the book regardless of price.
            if (incoming.type != OrderType::MARKET && !priceOk(level_price)) break;

            PriceLevel& level = it->second;
            while (incoming.remaining > 0 && !level.orders.empty()) {
                Order& resting = level.orders.front();
                Quantity fill = std::min(incoming.remaining, resting.remaining);

                Trade t;
                t.price = level_price;   // trade executes at the resting order's price
                t.quantity = fill;
                t.timestamp = incoming.timestamp;  // reuse arrival time; avoids a
                                                   // clock syscall per trade on the hot path
                if (incoming.side == Side::BID) {
                    t.buy_order_id = incoming.id;
                    t.sell_order_id = resting.id;
                } else {
                    t.buy_order_id = resting.id;
                    t.sell_order_id = incoming.id;
                }
                trades.push_back(t);
                ++total_trades_;

                incoming.remaining -= fill;
                resting.remaining -= fill;
                level.total_quantity -= fill;

                if (resting.remaining == 0) {
                    order_index_.erase(resting.id);
                    level.orders.pop_front();
                }
            }
            if (level.orders.empty()) {
                oppositeSide.erase(it);
            }
        }
    };

    if (incoming.side == Side::BID) {
        // Buy matches asks priced <= incoming price
        tryMatch(asks_, [&](Ticks p) { return p <= incoming.price; });
    } else {
        // Sell matches bids priced >= incoming price
        tryMatch(bids_, [&](Ticks p) { return p >= incoming.price; });
    }
    return trades;
}

void OrderBook::rest(const Order& order) {
    if (order.side == Side::BID) {
        PriceLevel& level = bids_[order.price];
        level.price = order.price;
        level.total_quantity += order.remaining;
        level.orders.push_back(order);
    } else {
        PriceLevel& level = asks_[order.price];
        level.price = order.price;
        level.total_quantity += order.remaining;
        level.orders.push_back(order);
    }
    order_index_[order.id] = OrderLocation{order.side, order.price};
}

bool OrderBook::cancelOrder(OrderId id) {
    auto idx = order_index_.find(id);
    if (idx == order_index_.end()) return false;

    OrderLocation loc = idx->second;
    auto removeFrom = [&](auto& sideMap) -> bool {
        auto lvlIt = sideMap.find(loc.price);
        if (lvlIt == sideMap.end()) return false;
        PriceLevel& level = lvlIt->second;
        for (auto it = level.orders.begin(); it != level.orders.end(); ++it) {
            if (it->id == id) {
                level.total_quantity -= it->remaining;
                level.orders.erase(it);
                if (level.orders.empty()) sideMap.erase(lvlIt);
                return true;
            }
        }
        return false;
    };

    bool removed = (loc.side == Side::BID) ? removeFrom(bids_) : removeFrom(asks_);
    if (removed) {
        order_index_.erase(idx);
        ++total_cancels_;
    }
    return removed;
}

std::vector<Trade> OrderBook::modifyOrder(OrderId id, Quantity new_quantity, Ticks new_price) {
    auto idx = order_index_.find(id);
    if (idx == order_index_.end()) return {};

    OrderLocation loc = idx->second;

    // Find the existing order to preserve its side/type/timestamp.
    Order existing;
    bool found = false;

    auto lookup = [&](auto& m) {
        auto lvlIt = m.find(loc.price);
        if (lvlIt == m.end()) return;
        for (auto& o : lvlIt->second.orders) {
            if (o.id == id) { existing = o; found = true; break; }
        }
    };
    if (loc.side == Side::BID) lookup(bids_); else lookup(asks_);
    if (!found) return {};

    // Quantity-only decrease at same price: retain time priority.
    if (new_price == loc.price && new_quantity < existing.remaining) {
        auto adjust = [&](auto& m) {
            auto lvlIt = m.find(loc.price);
            PriceLevel& level = lvlIt->second;
            for (auto& o : level.orders) {
                if (o.id == id) {
                    Quantity diff = o.remaining - new_quantity;
                    o.remaining = new_quantity;
                    o.quantity = new_quantity;
                    level.total_quantity -= diff;
                    break;
                }
            }
        };
        if (loc.side == Side::BID) adjust(bids_); else adjust(asks_);
        return {};
    }

    // Otherwise: cancel and re-add (loses time priority; may cross the book).
    cancelOrder(id);
    Order updated = existing;
    updated.price = new_price;
    updated.quantity = new_quantity;
    updated.remaining = new_quantity;
    updated.timestamp = nowNanos();
    return addOrder(updated);
}

std::optional<Ticks> OrderBook::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Ticks> OrderBook::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<Ticks> OrderBook::spread() const {
    if (bids_.empty() || asks_.empty()) return std::nullopt;
    return asks_.begin()->first - bids_.begin()->first;
}

std::optional<double> OrderBook::midPrice() const {
    if (bids_.empty() || asks_.empty()) return std::nullopt;
    return (ticksToPrice(bids_.begin()->first) + ticksToPrice(asks_.begin()->first)) / 2.0;
}

template <typename BookSide>
std::vector<std::pair<Ticks, Quantity>> OrderBook::depth(const BookSide& side, int levels) const {
    std::vector<std::pair<Ticks, Quantity>> out;
    int count = 0;
    for (const auto& [price, level] : side) {
        if (count++ >= levels) break;
        out.emplace_back(price, level.total_quantity);
    }
    return out;
}

std::vector<std::pair<Ticks, Quantity>> OrderBook::bidDepth(int levels) const {
    return depth(bids_, levels);
}

std::vector<std::pair<Ticks, Quantity>> OrderBook::askDepth(int levels) const {
    return depth(asks_, levels);
}

} // namespace orderbook
