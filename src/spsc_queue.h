#pragma once

#include <atomic>
#include <vector>
#include <cstddef>
#include <utility>

namespace orderbook {

// Cache line size: 64 bytes on x86-64 and Apple Silicon.
inline constexpr std::size_t CACHE_LINE = 64;

// ---------------------------------------------------------------------------
// Lock-free bounded Single-Producer / Single-Consumer (SPSC) ring buffer.
//
// CONTRACT: exactly ONE thread calls push(); exactly ONE (different) thread
// calls pop(). Violating this (two producers or two consumers) is undefined —
// the lock-free correctness proof relies on there being a single writer for
// each index.
//
// HOW IT'S LOCK-FREE:
//   Two indices into a circular array:
//     tail_  -> where the producer writes next   (only the producer mutates it)
//     head_  -> where the consumer reads next     (only the consumer mutates it)
//   The producer and consumer never modify the same index, so they never need
//   a mutex. They coordinate purely through atomic loads/stores.
//
// MEMORY ORDERING (the crux):
//   push():  write buffer[tail]  ->  tail_.store(RELEASE)
//   pop():   tail_.load(ACQUIRE) ->  read buffer[head]
//   The release/acquire pair creates a "happens-before" edge: everything the
//   producer wrote to the slot BEFORE the release store is guaranteed visible
//   to the consumer AFTER its acquire load. Without this, the consumer could
//   see the updated index but stale slot data (CPUs/compilers reorder writes).
//
// FALSE SHARING:
//   head_ and tail_ are placed on separate cache lines (alignas(CACHE_LINE)).
//   If they shared a line, every producer write to tail_ would invalidate the
//   consumer's cached copy of head_ (and vice versa), causing constant cache-
//   coherency traffic — "false sharing" — that silently destroys throughput.
// ---------------------------------------------------------------------------
template <typename T>
class SpscQueue {
public:
    // `capacity` is the max number of items that can be buffered. Internally we
    // allocate one extra slot so a full ring is distinguishable from an empty one.
    explicit SpscQueue(std::size_t capacity)
        : capacity_(capacity + 1), buffer_(capacity + 1) {}

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    // PRODUCER ONLY. Returns false if the ring is full (caller decides to
    // spin/backoff/drop).
    bool push(T item) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);  // only we write tail_
        const std::size_t next = increment(tail);
        // Full when advancing tail would land on head. Acquire so we observe the
        // consumer's most recent progress.
        if (next == head_.load(std::memory_order_acquire)) {
            return false;
        }
        buffer_[tail] = std::move(item);                    // 1. write the slot
        tail_.store(next, std::memory_order_release);       // 2. publish it
        return true;
    }

    // CONSUMER ONLY. Returns false if the ring is empty.
    bool pop(T& out) {
        const std::size_t head = head_.load(std::memory_order_relaxed);  // only we write head_
        // Empty when head has caught up to tail. Acquire so we observe the
        // producer's most recent slot write.
        if (head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        out = std::move(buffer_[head]);                     // 1. read the slot
        head_.store(increment(head), std::memory_order_release); // 2. free it
        return true;
    }

    // Approximate emptiness check (safe to call from either side).
    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    std::size_t capacity() const { return capacity_ - 1; }

private:
    std::size_t increment(std::size_t i) const { return (i + 1) % capacity_; }

    const std::size_t capacity_;
    std::vector<T> buffer_;

    // Each index on its own cache line to prevent false sharing.
    alignas(CACHE_LINE) std::atomic<std::size_t> head_{0};  // consumer writes, producer reads
    alignas(CACHE_LINE) std::atomic<std::size_t> tail_{0};  // producer writes, consumer reads
    // Trailing pad so whatever is laid out after this object can't land on
    // tail_'s cache line.
    char padding_[CACHE_LINE];
};

} // namespace orderbook
