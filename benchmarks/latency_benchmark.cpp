// Measures PER-ORDER latency percentiles (p50/p90/p99/p99.9/max), not just average.
// HFT firms care about tail latency, so this records each order's latency into a
// histogram and reports the distribution.
//
// Caveat: timing each individual op adds clock-read overhead (~20-40ns per call),
// so absolute numbers here are slightly inflated vs. the batch-average benchmark.
// The DISTRIBUTION (how much worse p99 is than p50) is the meaningful signal.

#include "../src/order_book.h"
#include "../src/types.h"

#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <cstdint>

using namespace orderbook;

int main(int argc, char* argv[]) {
    int num_orders = 1000000;
    if (argc > 1) num_orders = std::stoi(argv[1]);

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int64_t> price_dist(9900, 10100);
    std::uniform_int_distribution<int> qty_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);

    std::cout << "Generating " << num_orders << " orders...\n";
    std::vector<Order> orders;
    orders.reserve(num_orders);
    for (int i = 0; i < num_orders; ++i) {
        Order o;
        o.id = static_cast<OrderId>(i + 1);
        o.side = side_dist(rng) ? Side::BID : Side::ASK;
        o.type = OrderType::LIMIT;
        o.price = price_dist(rng);
        o.quantity = qty_dist(rng);
        o.remaining = o.quantity;
        o.timestamp = static_cast<Timestamp>(i + 1);
        orders.push_back(o);
    }

    OrderBook book("BENCH", static_cast<size_t>(num_orders));  // reserve to avoid rehash spikes
    std::vector<uint64_t> latencies;   // nanoseconds per order
    latencies.reserve(num_orders);

    std::cout << "Measuring per-order latency...\n";
    for (auto& o : orders) {
        auto t0 = std::chrono::high_resolution_clock::now();
        book.addOrder(o);
        auto t1 = std::chrono::high_resolution_clock::now();
        latencies.push_back(
            static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }

    std::sort(latencies.begin(), latencies.end());

    auto pct = [&](double p) -> uint64_t {
        size_t idx = static_cast<size_t>(p * (latencies.size() - 1));
        return latencies[idx];
    };

    uint64_t sum = 0;
    for (uint64_t l : latencies) sum += l;
    double avg = static_cast<double>(sum) / latencies.size();

    std::cout << "\n=== Per-Order Latency Distribution ===\n";
    std::cout << "Orders measured:  " << num_orders << "\n";
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "  min:    " << latencies.front() << " ns\n";
    std::cout << "  p50:    " << pct(0.50)  << " ns\n";
    std::cout << "  p90:    " << pct(0.90)  << " ns\n";
    std::cout << "  p99:    " << pct(0.99)  << " ns\n";
    std::cout << "  p99.9:  " << pct(0.999) << " ns\n";
    std::cout << "  max:    " << latencies.back() << " ns\n";
    std::cout << std::setprecision(1);
    std::cout << "  avg:    " << avg << " ns\n";

    return 0;
}
