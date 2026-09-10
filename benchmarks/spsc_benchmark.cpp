// Head-to-head: mutex+condition_variable queue vs. lock-free SPSC ring buffer.
// One producer thread and one consumer thread hand off N items; we measure
// end-to-end throughput (items/sec) for each design.

#include "../src/spsc_queue.h"

#include <iostream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <cstdint>

using namespace orderbook;
using clock_t_ = std::chrono::high_resolution_clock;

// ---- Baseline: a conventional mutex + condition_variable queue ----
template <typename T>
class MutexQueue {
public:
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(m_);
            q_.push(std::move(item));
        }
        cv_.notify_one();
    }
    T pop() {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [this] { return !q_.empty(); });
        T item = std::move(q_.front());
        q_.pop();
        return item;
    }
private:
    std::queue<T> q_;
    std::mutex m_;
    std::condition_variable cv_;
};

static double run_mutex(uint64_t N) {
    MutexQueue<uint64_t> q;
    auto t0 = clock_t_::now();
    std::thread consumer([&]() {
        for (uint64_t i = 0; i < N; ++i) { volatile uint64_t v = q.pop(); (void)v; }
    });
    std::thread producer([&]() {
        for (uint64_t i = 0; i < N; ++i) q.push(i);
    });
    producer.join();
    consumer.join();
    auto t1 = clock_t_::now();
    double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    return static_cast<double>(N) * 1e9 / ns;
}

static double run_spsc(uint64_t N) {
    SpscQueue<uint64_t> q(1024);
    auto t0 = clock_t_::now();
    std::thread consumer([&]() {
        uint64_t v;
        for (uint64_t i = 0; i < N; ++i) {
            while (!q.pop(v)) { /* spin */ }
        }
    });
    std::thread producer([&]() {
        for (uint64_t i = 0; i < N; ++i) {
            while (!q.push(i)) { /* spin */ }
        }
    });
    producer.join();
    consumer.join();
    auto t1 = clock_t_::now();
    double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    return static_cast<double>(N) * 1e9 / ns;
}

int main(int argc, char* argv[]) {
    uint64_t N = 10'000'000;
    if (argc > 1) N = std::stoull(argv[1]);

    std::cout << "Handing off " << N << " items, one producer -> one consumer...\n\n";

    double mutex_rate = run_mutex(N);
    double spsc_rate  = run_spsc(N);

    std::cout << std::fixed << std::setprecision(0);
    std::cout << "=== Queue Throughput (single producer / single consumer) ===\n";
    std::cout << "  Mutex + condvar queue:  " << mutex_rate << " items/sec\n";
    std::cout << "  Lock-free SPSC queue:   " << spsc_rate  << " items/sec\n";
    std::cout << std::setprecision(1);
    std::cout << "  Speedup:                " << (spsc_rate / mutex_rate) << "x\n";

    return 0;
}
