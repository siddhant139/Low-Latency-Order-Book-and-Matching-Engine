#pragma once

#include "order_book.h"
#include "analytics.h"
#include "spsc_queue.h"

#include <string>
#include <atomic>
#include <thread>

namespace orderbook {

// TCP network front-end for the matching engine, built as a classic low-latency
// reactor: ONE I/O thread and ONE matching thread, connected by a lock-free
// single-producer/single-consumer (SPSC) ring buffer.
//
//   many client sockets
//        │  (poll)
//        ▼
//   [ I/O thread ]  ── the SINGLE PRODUCER: polls all sockets, reads bytes,
//        │              splits into order lines, pushes them into the SPSC queue
//        ▼
//   [ SPSC queue ] ── lock-free ring buffer (no mutex on the hot path)
//        │
//        ▼
//   [ matching thread ] ── the SINGLE CONSUMER: pops orders, applies them to the
//        │                  OrderBook (touched by this thread ONLY -> no locks),
//        ▼                  and sends the execution report back to the client
//   OrderBook
//
// A single I/O thread means the SPSC queue has exactly one producer, satisfying
// the SPSC contract. A single matching thread means the OrderBook needs no locks.
class OrderServer {
public:
    OrderServer(int port, const std::string& symbol);
    ~OrderServer();

    OrderServer(const OrderServer&) = delete;
    OrderServer& operator=(const OrderServer&) = delete;

    void start();  // blocking: runs the I/O reactor (matching thread runs in background)
    void stop();

private:
    struct Job {
        std::string line;   // raw order line from a client
        int client_fd;      // where to send the execution report
    };

    int port_;
    int listen_fd_;
    std::atomic<bool> running_;

    OrderBook book_;         // touched ONLY by the matching thread (lock-free)
    Analytics analytics_;    // touched ONLY by the matching thread

    SpscQueue<Job> queue_;   // I/O thread produces -> matching thread consumes
    std::thread matching_thread_;

    void ioLoop();                                  // PRODUCER: poll + read + enqueue
    void matchingLoop();                            // CONSUMER: dequeue + apply + reply
    std::string process(const std::string& line);   // runs on the matching thread only
};

} // namespace orderbook
