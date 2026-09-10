#include "analytics.h"

#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace orderbook {

void Analytics::recordTrade(const Trade& t) {
    double price = ticksToPrice(t.price);
    double qty = static_cast<double>(t.quantity);

    ++trade_count_;
    total_volume_ += t.quantity;
    notional_ += price * qty;

    if (trade_count_ == 1) {
        high_ = price;
        low_ = price;
    } else {
        if (price > high_) high_ = price;
        if (price < low_) low_ = price;
    }
    last_ = price;

    prices_.push_back(price);
}

double Analytics::vwap() const {
    if (total_volume_ == 0) return 0.0;
    return notional_ / static_cast<double>(total_volume_);
}

uint64_t Analytics::totalVolume() const { return total_volume_; }

uint64_t Analytics::tradeCount() const { return trade_count_; }

double Analytics::realizedVolatility() const {
    if (prices_.size() < 2) return 0.0;

    // Compute log returns
    std::vector<double> returns;
    returns.reserve(prices_.size() - 1);
    for (size_t i = 1; i < prices_.size(); ++i) {
        returns.push_back(std::log(prices_[i] / prices_[i - 1]));
    }

    // Mean of returns
    double sum = std::accumulate(returns.begin(), returns.end(), 0.0);
    double mean = sum / static_cast<double>(returns.size());

    // Sample standard deviation (divide by n-1)
    double sq_sum = 0.0;
    for (double r : returns) {
        double diff = r - mean;
        sq_sum += diff * diff;
    }
    double variance = sq_sum / static_cast<double>(returns.size() - 1);
    return std::sqrt(variance);
}

double Analytics::highPrice() const { return high_; }

double Analytics::lowPrice() const { return low_; }

double Analytics::lastPrice() const { return last_; }

std::string Analytics::summary() const {
    std::ostringstream os;
    os << "=== Trading Analytics ===\n";
    os << "Trade count:          " << trade_count_ << "\n";
    os << "Total volume:         " << total_volume_ << "\n";
    os << std::fixed;
    os << std::setprecision(2);
    os << "VWAP:                 $" << vwap() << "\n";
    os << "High:                 $" << high_ << "\n";
    os << "Low:                  $" << low_ << "\n";
    os << "Last:                 $" << last_ << "\n";
    os << std::setprecision(4);
    os << "Realized volatility:  " << realizedVolatility() << "\n";
    return os.str();
}

} // namespace orderbook
