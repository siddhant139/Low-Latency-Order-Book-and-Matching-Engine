#include "../src/order_book.h"
#include "../src/types.h"
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <iomanip>

using namespace orderbook;

// Pre-generate a random but realistic order flow to avoid RNG cost in the timed loop.
struct GenOrder { Order order; bool is_cancel; OrderId cancel_id; };

int main(int argc, char* argv[]) {
    int num_orders = 1000000;
    if (argc > 1) num_orders = std::stoi(argv[1]);

    std::mt19937_64 rng(42);
    // Prices cluster around 100.00 (10000 ticks) with a spread.
    std::uniform_int_distribution<int64_t> price_dist(9900, 10100); // ticks: 99.00 - 101.00
    std::uniform_int_distribution<int> qty_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> action_dist(0, 99);

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

    std::cout << "Running benchmark (add-order matching)...\n";
    auto start = std::chrono::high_resolution_clock::now();
    uint64_t trade_count = 0;
    for (auto& o : orders) {
        auto trades = book.addOrder(o);
        trade_count += trades.size();
    }
    auto elapsed = std::chrono::high_resolution_clock::now() - start;

    double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    double per_op = ns / num_orders;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n=== Matching Engine Benchmark ===\n";
    std::cout << "Orders processed:   " << num_orders << "\n";
    std::cout << "Trades generated:   " << trade_count << "\n";
    std::cout << "Total time:         " << (ns / 1e6) << " ms\n";
    std::cout << "Avg latency:        " << per_op << " ns/order\n";
    std::cout << "Throughput:         " << std::setprecision(0) << (1e9 / per_op) << " orders/sec\n";
    std::cout << "Resting orders:     " << book.restingOrderCount() << "\n";
    std::cout << "Bid levels:         " << book.bidLevels() << "\n";
    std::cout << "Ask levels:         " << book.askLevels() << "\n";

    return 0;
}
