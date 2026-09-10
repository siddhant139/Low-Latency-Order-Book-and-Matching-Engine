#include <iostream>
#include <string>
#include <cmath>
#include "../src/analytics.h"
#include "../src/types.h"

int tests_passed = 0, tests_failed = 0;

void check(bool condition, const std::string& name) {
    if (condition) { std::cout << "  PASS: " << name << std::endl; tests_passed++; }
    else { std::cout << "  FAIL: " << name << std::endl; tests_failed++; }
}

using namespace orderbook;

static Trade mkTrade(double price, Quantity qty) {
    Trade t;
    t.price = priceToTicks(price);
    t.quantity = qty;
    return t;
}

int main() {
    std::cout << "=== Analytics Tests ===" << std::endl;

    // 1. Empty analytics
    {
        Analytics a;
        check(a.vwap() == 0.0, "empty: vwap == 0");
        check(a.tradeCount() == 0, "empty: tradeCount == 0");
        check(a.totalVolume() == 0, "empty: totalVolume == 0");
        check(a.realizedVolatility() == 0.0, "empty: realizedVolatility == 0");
    }

    // 2. Single trade
    {
        Analytics a;
        a.recordTrade(mkTrade(100.00, 10));
        check(a.tradeCount() == 1, "single: tradeCount == 1");
        check(a.totalVolume() == 10, "single: totalVolume == 10");
        check(std::abs(a.vwap() - 100.00) < 1e-6, "single: vwap == 100.00");
        check(std::abs(a.highPrice() - 100.00) < 1e-6, "single: high == 100.00");
        check(std::abs(a.lowPrice() - 100.00) < 1e-6, "single: low == 100.00");
        check(std::abs(a.lastPrice() - 100.00) < 1e-6, "single: last == 100.00");
    }

    // 3. VWAP weighted
    {
        Analytics a;
        a.recordTrade(mkTrade(100.00, 10));
        a.recordTrade(mkTrade(200.00, 30));
        // vwap = (100*10 + 200*30) / 40 = 7000/40 = 175.00
        check(std::abs(a.vwap() - 175.00) < 1e-6, "vwap_weighted: vwap == 175.00");
    }

    // 4. Total volume
    {
        Analytics a;
        a.recordTrade(mkTrade(100.00, 10));
        a.recordTrade(mkTrade(101.00, 20));
        a.recordTrade(mkTrade(102.00, 30));
        check(a.totalVolume() == 60, "total_volume: totalVolume == 60");
    }

    // 5. High/low/last
    {
        Analytics a;
        a.recordTrade(mkTrade(100.00, 1));
        a.recordTrade(mkTrade(110.00, 1));
        a.recordTrade(mkTrade(90.00, 1));
        a.recordTrade(mkTrade(105.00, 1));
        check(std::abs(a.highPrice() - 110.00) < 1e-6, "high_low_last: high == 110.00");
        check(std::abs(a.lowPrice() - 90.00) < 1e-6, "high_low_last: low == 90.00");
        check(std::abs(a.lastPrice() - 105.00) < 1e-6, "high_low_last: last == 105.00");
    }

    // 6. Volatility zero for < 2 trades
    {
        Analytics a;
        a.recordTrade(mkTrade(100.00, 1));
        check(a.realizedVolatility() == 0.0, "vol_single: realizedVolatility == 0");
    }

    // 7. Volatility positive for varying prices
    {
        Analytics a;
        a.recordTrade(mkTrade(100.00, 1));
        a.recordTrade(mkTrade(110.00, 1));
        a.recordTrade(mkTrade(105.00, 1));
        a.recordTrade(mkTrade(115.00, 1));
        check(a.realizedVolatility() > 0.0, "vol_positive: realizedVolatility > 0");
    }

    // 8. Volatility zero for constant prices
    {
        Analytics a;
        a.recordTrade(mkTrade(100.00, 1));
        a.recordTrade(mkTrade(100.00, 1));
        a.recordTrade(mkTrade(100.00, 1));
        check(a.realizedVolatility() < 1e-9, "vol_constant: realizedVolatility ~= 0");
    }

    std::cout << "\nResults: " << tests_passed << " passed, " << tests_failed << " failed." << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
