#pragma once

#include "types.h"
#include <vector>
#include <string>

namespace orderbook {

// Computes trading analytics from a stream of executed trades.
class Analytics {
public:
    void recordTrade(const Trade& t);

    // Volume-Weighted Average Price = sum(price_i * qty_i) / sum(qty_i)
    double vwap() const;

    // Total traded volume (sum of quantities)
    uint64_t totalVolume() const;

    // Number of trades
    uint64_t tradeCount() const;

    // Realized volatility: standard deviation of trade-to-trade log returns,
    // computed over executed trade prices. Returns 0 if < 2 trades.
    double realizedVolatility() const;

    // Highest and lowest execution prices (in dollars). 0 if no trades.
    double highPrice() const;
    double lowPrice() const;

    // Last execution price in dollars (0 if none).
    double lastPrice() const;

    std::string summary() const;

private:
    uint64_t trade_count_ = 0;
    uint64_t total_volume_ = 0;
    double notional_ = 0.0;        // sum(price_dollars * qty) for VWAP
    double high_ = 0.0;
    double low_ = 0.0;
    double last_ = 0.0;
    std::vector<double> prices_;   // execution prices in dollars, for volatility
};

} // namespace orderbook
