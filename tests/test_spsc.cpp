// Correctness tests for the lock-free SPSC ring buffer.
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>

#include "../src/spsc_queue.h"

using namespace orderbook;

int tests_passed = 0, tests_failed = 0;
void check(bool condition, const std::string& name) {
    if (condition) { std::cout << "  PASS: " << name << std::endl; tests_passed++; }
    else { std::cout << "  FAIL: " << name << std::endl; tests_failed++; }
}

int main() {
    std::cout << "=== Lock-Free SPSC Queue Tests ===\n";

    // ---- Single-threaded basic semantics ----
    {
        std::cout << "\n[basic push/pop/empty/full]\n";
        SpscQueue<int> q(4);
        int out = -1;
        check(q.empty(), "new queue is empty");
        check(!q.pop(out), "pop on empty returns false");

        check(q.push(1), "push 1");
        check(q.push(2), "push 2");
        check(q.push(3), "push 3");
        check(q.push(4), "push 4 (fills capacity)");
        check(!q.push(5), "push on full returns false");

        check(q.pop(out) && out == 1, "pop returns 1 (FIFO)");
        check(q.pop(out) && out == 2, "pop returns 2");
        check(q.push(5), "can push again after popping");
        check(q.pop(out) && out == 3, "pop returns 3");
        check(q.pop(out) && out == 4, "pop returns 4");
        check(q.pop(out) && out == 5, "pop returns 5");
        check(!q.pop(out), "empty again");
    }

    // ---- Concurrent producer + consumer, high volume ----
    {
        std::cout << "\n[concurrent: 5,000,000 items across two threads]\n";
        const uint64_t N = 5'000'000;
        SpscQueue<uint64_t> q(1024);

        // Consumer verifies it receives 0,1,2,...,N-1 in exact order with no gaps.
        bool order_ok = true;
        uint64_t received = 0;
        std::thread consumer([&]() {
            uint64_t expected = 0;
            uint64_t v;
            while (expected < N) {
                if (q.pop(v)) {
                    if (v != expected) { order_ok = false; }
                    ++expected;
                    ++received;
                }
                // else: spin (queue momentarily empty)
            }
        });

        std::thread producer([&]() {
            for (uint64_t i = 0; i < N; ++i) {
                while (!q.push(i)) { /* spin while full */ }
            }
        });

        producer.join();
        consumer.join();

        check(received == N, "consumer received exactly N items (no loss)");
        check(order_ok, "all items arrived in strict FIFO order (no reordering/corruption)");
    }

    // ---- Concurrent with a non-trivial type (std::string) ----
    {
        std::cout << "\n[concurrent: string payloads]\n";
        const int N = 100000;
        SpscQueue<std::string> q(256);
        int received = 0;
        bool ok = true;

        std::thread consumer([&]() {
            std::string s;
            int expected = 0;
            while (expected < N) {
                if (q.pop(s)) {
                    if (s != ("item_" + std::to_string(expected))) ok = false;
                    ++expected;
                    ++received;
                }
            }
        });
        std::thread producer([&]() {
            for (int i = 0; i < N; ++i) {
                while (!q.push("item_" + std::to_string(i))) {}
            }
        });
        producer.join();
        consumer.join();

        check(received == N, "received all string items");
        check(ok, "string payloads intact and in order");
    }

    std::cout << "\n=== Results: " << tests_passed << " passed, " << tests_failed << " failed ===\n";
    return tests_failed > 0 ? 1 : 0;
}
