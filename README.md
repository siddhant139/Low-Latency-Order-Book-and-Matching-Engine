# order-book — Limit Order Book & Matching Engine

A high-performance limit order book and matching engine in **C++17**, implementing price-time priority matching, L2 market data, and trading analytics. Built from scratch to model the core of an exchange matching engine used by trading firms. Uses **fixed-point arithmetic** for prices (never floating point), cache-friendly data structures, and **O(1) order cancellation**.

---

## Architecture

```
                          ┌─────────────────────────────────────────────────────┐
                          │              Matching Engine                        │
                          │                                                     │
  ┌──────────────┐        │  ┌───────────────────┐  ┌───────────────────────┐   │        ┌──────────────┐
  │  Order Feed  │        │  │    BID Book       │  │      ASK Book         │   │        │  Trade Feed  │
  │  (CSV/stdin) │───┐    │  │  std::map         │  │  std::map             │   │───────>│  (executions)│
  └──────────────┘   │    │  │  <price,deque>    │  │  <price,deque>        │   │        └──────────────┘
                     │    │  │  greater<> (desc) │  │  less<> (asc)         │   │
                     v    │  │                   │  │                       │   │        ┌──────────────┐
               ┌──────────┤  │  105.50 [O4][O7]  │  │  105.75 [O2][O5]      │   │───────>│ L2 Market    │
               │ Gateway  │  │  105.25 [O1][O3]  │  │  106.00 [O6][O9]      │   │        │ Data         │
               │          │  │  105.00 [O8]      │  │  106.25 [O10]         │   │        └──────────────┘
               │  Parser: │  └───────────────────┘  └───────────────────────┘   │
               │  N (NEW) │                                                     │        ┌──────────────┐
               │  C (CXL) │  ┌─────────────────────────────────────────────┐    │───────>│  Analytics   │
               │  M (MOD) │  │  Order Index: unordered_map<id, iterator>   │    │        │  VWAP, Vol,  │
               └──────────┤  │  O(1) cancel / modify by order ID           │    │        │  Spread      │
                          │  └─────────────────────────────────────────────┘    │        └──────────────┘
                          └─────────────────────────────────────────────────────┘

  Each price level is a std::deque — FIFO queue for time priority.
  Orders at the same price fill oldest-first (front of deque).
```

---

## Key Design Decisions

| Decision | Rationale |
|---|---|
| **Fixed-point prices (`int64` ticks)** | Floating point has rounding errors: `0.1 + 0.2 != 0.3`. Exchanges represent prices as integer ticks. Prevents penny-rounding bugs and makes price comparison exact. |
| **`std::map` for price levels** | Keeps levels sorted by price automatically. Best bid/ask is `begin()` in O(1) amortized. Bids use `std::greater` (highest first), asks use `std::less` (lowest first). |
| **`std::deque` per price level** | Orders at the same price are FIFO (time priority). `deque` gives O(1) `push_back` (new order) and `pop_front` (fill oldest) with better cache locality than `std::list`. |
| **`unordered_map` order index** | Maps `order_id → location` for O(1) cancel/modify without scanning the book. |
| **Price-time priority** | The matching rule used by most major exchanges (NYSE, NASDAQ, CME): best price first, then earliest arrival at that price. |

---

## Matching Algorithm

The engine uses **price-time priority** (also called FIFO matching):

1. **Price priority** — an incoming buy order matches against the **lowest ask** first; an incoming sell matches against the **highest bid** first.
2. **Time priority** — within the same price level, the order that arrived **earliest** fills first (front of the deque).
3. **Passive price execution** — trades execute at the **resting** (passive) order's price, not the aggressor's limit.

### Worked Example

The ask side of the book has:

| Price | Queue (arrival order) |
|-------|----------------------|
| 105.75 | `[A1: 50 shares]` `[A2: 100 shares]` |
| 106.00 | `[A3: 200 shares]` |

An incoming **BUY 180 shares @ LIMIT 106.00** arrives (the aggressor):

| Step | Action | Trade | Remaining |
|------|--------|-------|-----------|
| 1 | Match vs A1 at **105.75** (best ask, earliest) | **50 @ 105.75** | 130 to fill |
| 2 | Match vs A2 at **105.75** (same level, next in FIFO) | **100 @ 105.75** | 30 to fill |
| 3 | 105.75 level exhausted → move to **106.00** | | |
| 4 | Match vs A3 at **106.00** (partial fill) | **30 @ 106.00** | 0 — fully filled |

A3 has 170 shares remaining and stays on the book. The buyer paid the passive price at each level — **not** their limit of 106.00 for the first two fills.

---

## Order Types

| Type | Behavior |
|------|----------|
| **LIMIT** | Matches at the limit price or better. Any unfilled quantity **rests** on the book at the limit price until filled or cancelled. |
| **MARKET** | Matches immediately against the best available price(s). Any unfilled remainder is **discarded** (never rests on the book). |
| **IOC** (Immediate-Or-Cancel) | Matches whatever is available *now* at the limit price, then **cancels** any unfilled remainder (never rests). A market order with a price ceiling/floor. |
| **FOK** (Fill-Or-Kill) | **All-or-nothing**: fills the *entire* quantity immediately at the limit price, or **kills** the whole order with zero fills. Feasibility is checked before any matching, so a killed FOK leaves the book untouched. |

---

## Feed Format

Orders are submitted as CSV — one order per line, via file or stdin.

```
N,<id>,<side>,<type>,<price>,<qty>   # New order:  N,1,BID,LIMIT,105.25,100
C,<id>                               # Cancel:     C,1
M,<id>,<qty>,<price>                 # Modify:     M,1,150,105.50
```

| Field | Values |
|-------|--------|
| `side` | `BID` or `ASK` |
| `type` | `LIMIT`, `MARKET`, `IOC`, or `FOK` |
| `price` | Decimal string (converted to integer ticks internally) |
| `qty` | Positive integer |

Lines starting with `#` are comments and are ignored. Blank lines are skipped.

---

## Build & Run

### Direct compilation

```bash
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread -I src \
    src/main.cpp src/order_book.cpp src/gateway.cpp src/market_data.cpp \
    src/analytics.cpp src/order_server.cpp \
    -o build/order_book
```

### Run — batch mode (replay a feed file)

```bash
# Run against sample feed with verbose trade + L2 output
./build/order_book -s AAPL -f data/sample_orders.csv -v

# Or pipe orders via stdin
cat data/sample_orders.csv | ./build/order_book -s AAPL
```

### Run — network mode (live matching-engine server)

Accept orders over TCP, like a real exchange gateway:

```bash
# Terminal A: start the matching engine on port 9001
./build/order_book --serve -p 9001 -s AAPL

# Terminal B: send orders and read execution reports
python3 benchmarks/test_client.py 9001
```

Each connection streams newline-delimited orders (`N,id,side,type,price,qty` / `C,id` / `M,id,qty,price`) and receives an execution report per order. Multiple clients can connect concurrently; all orders funnel through a single matching thread (see Concurrency Model below).

### CMake

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run
./order_book -s AAPL -f ../data/sample_orders.csv -v
```

---

## Example Output

### L2 Order Book Snapshot

```
=============== AAPL  L2 Order Book ===============

        ASK                 QTY        ORDERS
  ──────────────────────────────────────────────
        107.25              200             1
        107.00              350             2
        106.75              150             1
        106.50              500             3
        106.25              275             2
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        106.00              300             2
        105.75              450             3
        105.50              600             4
        105.25              200             1
        105.00              150             1
        BID                 QTY        ORDERS

  Spread: 0.25   Mid: 106.125
```

### Trading Analytics

```
═══════════════ Trading Analytics ═══════════════
  Symbol:              AAPL
  Trade count:         1,247
  Total volume:        384,500 shares
  VWAP:                105.8732
  High:                107.25
  Low:                 104.50
  Last:                106.00
  Realized volatility: 0.0142 (1.42%)
═════════════════════════════════════════════════
```

---

## Analytics

| Metric | Formula | Description |
|--------|---------|-------------|
| **VWAP** | `Σ(price × qty) / Σ(qty)` | Volume-Weighted Average Price. The average price paid per share, weighted by trade size. Institutional benchmark for execution quality. |
| **Realized Volatility** | `stddev(ln(Pᵢ / Pᵢ₋₁))` | Standard deviation of trade-to-trade log returns. Measures how much the price fluctuates between consecutive trades. |
| **Spread** | `best_ask − best_bid` | The gap between the lowest ask and highest bid. Tighter spreads indicate higher liquidity. |
| **Mid Price** | `(best_ask + best_bid) / 2` | Midpoint of the best bid and ask. Common reference price for fair value. |

---

## Benchmarks

### Aggregate throughput (mixed workload)

| Metric | Value |
|---|---|
| Orders processed | 1,000,000 |
| Trades generated | ~776,000 |
| Throughput | **~12.7M orders/sec** |
| Avg latency | **~79 ns/order** |

### Per-operation throughput (isolated hot paths)

| Operation | Rate |
|---|---|
| Insert (rest into book) | **~23–29M ops/sec** |
| Match (cross + trade) | **~28M ops/sec** |
| Cancel (remove resting) | **~28–30M ops/sec** |

_Per-operation rates vary run-to-run (roughly ±15%) depending on allocator and cache state; insert is the most variable because it grows the book across price levels._

### Per-order latency distribution

| Percentile | Latency |
|---|---|
| p50 | **~42–83 ns** |
| p90 | ~125–170 ns |
| p99 | ~292 ns |
| p99.9 | ~0.9–1.0 µs |
| max | ~14–130 µs |

_Measured on macOS, Apple Silicon, `-O2`, 1M orders. Two latency optimizations flattened the tail dramatically: (1) pre-reserving the order-index hash map (`OrderBook(symbol, expected_orders)`) eliminated `unordered_map` rehash stalls that previously caused a ~1.9 ms worst-case; (2) hoisting the clock read out of the per-trade hot path cut average latency ~20%._

### Build & run benchmarks

```bash
# Aggregate throughput + average latency
g++ -std=c++17 -O2 -I src benchmarks/benchmark.cpp src/order_book.cpp -o build/benchmark
./build/benchmark 1000000

# Per-operation rates (insert / match / cancel)
g++ -std=c++17 -O2 -I src benchmarks/ops_benchmark.cpp src/order_book.cpp -o build/ops_benchmark
./build/ops_benchmark 1000000

# Latency percentiles (p50 / p90 / p99 / p99.9 / max)
g++ -std=c++17 -O2 -I src benchmarks/latency_benchmark.cpp src/order_book.cpp -o build/latency_benchmark
./build/latency_benchmark 1000000

# Lock-free SPSC queue vs. mutex+condvar queue (ingress path)
g++ -std=c++17 -O2 -pthread -I src benchmarks/spsc_benchmark.cpp -o build/spsc_benchmark
./build/spsc_benchmark 10000000
```

### Ingress queue: lock-free vs. mutex

| Queue (1 producer → 1 consumer) | Throughput |
|---|---|
| Mutex + condition_variable | ~13–14M items/sec |
| **Lock-free SPSC ring buffer** | **~24–25M items/sec** |
| Speedup | **~1.7–2.0×** |

---

## Testing

```bash
# Matching engine — limit crosses, partial fills, market orders
g++ -std=c++17 -I src tests/test_matching.cpp src/order_book.cpp \
    -o build/test_matching && ./build/test_matching

# Cancel & modify — O(1) cancel, modify-in-place, modify losing priority
g++ -std=c++17 -I src tests/test_cancel_modify.cpp src/order_book.cpp \
    -o build/test_cancel_modify && ./build/test_cancel_modify

# L2 market data — depth snapshots, level aggregation
g++ -std=c++17 -I src tests/test_market_data.cpp src/order_book.cpp src/market_data.cpp \
    -o build/test_market_data && ./build/test_market_data

# Analytics — VWAP, volatility, spread tracking
g++ -std=c++17 -I src tests/test_analytics.cpp src/analytics.cpp \
    -o build/test_analytics && ./build/test_analytics

# IOC / FOK order types — partial-fill-then-cancel, all-or-nothing, kill
g++ -std=c++17 -I src tests/test_ioc_fok.cpp src/order_book.cpp \
    -o build/test_ioc_fok && ./build/test_ioc_fok

# Lock-free SPSC queue — FIFO correctness under concurrent producer/consumer
g++ -std=c++17 -pthread -I src tests/test_spsc.cpp \
    -o build/test_spsc && ./build/test_spsc
```

All suites pass: **121 tests** (31 matching + 16 cancel/modify + 23 market data + 18 analytics + 15 IOC/FOK + 18 SPSC).

---

## Concurrency Model (network mode)

The `--serve` mode uses a classic low-latency **reactor**: one I/O thread and one matching thread, connected by a **lock-free SPSC (single-producer/single-consumer) ring buffer**.

```
  client sockets
       │  poll()
       ▼
  [ I/O thread ]  ── SINGLE PRODUCER: polls all sockets, reads bytes,
       │              splits into order lines, pushes into the SPSC queue
       ▼
  [ lock-free SPSC ring buffer ]   ── no mutex on the hot path
       │
       ▼
  [ matching thread ]  ── SINGLE CONSUMER: pops orders, applies them to the
       │                   OrderBook (touched by this thread ONLY → no locks),
       ▼                   sends the execution report back to the client
  OrderBook
```

- **One I/O thread** `poll()`s every client socket and is the *sole producer* into the queue. A single producer is exactly what the SPSC contract requires.
- **One matching thread** is the *sole consumer* and the only thread that touches the `OrderBook`, so the matching hot path needs **no locks at all**.

**Why lock-free instead of a mutex queue?** A mutex queue occasionally blocks a thread and triggers an OS context switch (~µs), which shows up as tail latency. The SPSC ring buffer coordinates purely via two atomic indices with **acquire/release memory ordering**, and pads them onto separate cache lines to avoid **false sharing**. In the head-to-head benchmark it's **~1.7–2.0× faster** than a `mutex` + `condition_variable` queue (~24–25M vs ~13–14M items/sec).

See [`docs/lock_free_spsc_explainer.md`](docs/lock_free_spsc_explainer.md) for a full plain-English walkthrough of the memory ordering, false sharing, and the SPSC correctness argument.

---

## Project Structure

```
order-book/
├── src/
│   ├── types.h              # Core types: Order, Side, OrderType, fixed-point Price
│   ├── order_book.h         # OrderBook class declaration
│   ├── order_book.cpp       # Matching engine: add, cancel, modify, match logic
│   ├── gateway.h            # Feed parser declaration
│   ├── gateway.cpp          # CSV parser: N/C/M message handling
│   ├── market_data.h        # L2 market data publisher declaration
│   ├── market_data.cpp      # Order book depth snapshots, BBO, spread
│   ├── analytics.h          # Trading analytics declaration
│   ├── analytics.cpp        # VWAP, realized volatility, high/low/last
│   ├── order_server.h       # TCP matching-engine server declaration
│   ├── order_server.cpp     # Network ingress: poll I/O thread → SPSC queue → matching thread
│   ├── spsc_queue.h         # Lock-free single-producer/single-consumer ring buffer
│   └── main.cpp             # Entry point, CLI (batch + --serve modes)
├── tests/
│   ├── test_matching.cpp    # Matching engine tests
│   ├── test_cancel_modify.cpp # Cancel and modify tests
│   ├── test_market_data.cpp # L2 data tests
│   ├── test_analytics.cpp   # Analytics tests
│   ├── test_ioc_fok.cpp     # IOC / FOK order-type tests
│   └── test_spsc.cpp        # Lock-free SPSC queue correctness tests
├── benchmarks/
│   ├── benchmark.cpp        # Aggregate throughput + average latency
│   ├── ops_benchmark.cpp    # Per-operation rates (insert / match / cancel)
│   ├── latency_benchmark.cpp # Per-order latency percentiles (p50…max)
│   ├── spsc_benchmark.cpp   # Lock-free SPSC vs mutex queue throughput
│   └── test_client.py       # TCP client for the --serve matching engine
├── data/
│   └── sample_orders.csv    # Example order feed
├── docs/
│   └── lock_free_spsc_explainer.md  # Interview-prep deep-dive on the SPSC queue
├── CMakeLists.txt           # CMake build configuration
└── README.md
```

---

## CS / Quant Fundamentals Demonstrated

| Concept | Where |
|---------|-------|
| Fixed-point arithmetic | `types.h` — prices as integer ticks, never floating point |
| Price-time priority matching | `order_book.cpp` — core matching loop |
| Balanced BST (`std::map` / red-black tree) | Price level ordering — sorted bid and ask sides |
| FIFO queue (`std::deque`) | Time priority within a price level |
| Hash map O(1) lookup | `unordered_map` order index for cancel/modify |
| Cache-friendly data layout | `deque` contiguous blocks over pointer-chasing `list` |
| Latency-tail engineering | pre-reserved hash map + clock hoisted off hot path (see Benchmarks) |
| VWAP & realized volatility | `analytics.cpp` — streaming computation |
| L2 market data / order book depth | `market_data.cpp` — depth snapshots and BBO |
| POSIX sockets / TCP server | `order_server.cpp` — `poll()`-based I/O reactor |
| Producer-consumer concurrency | I/O thread → SPSC queue → matching thread |
| Lock-free programming | `spsc_queue.h` — atomic indices, acquire/release ordering |
| Cache-line / false-sharing awareness | `spsc_queue.h` — `alignas(64)` padded indices |
| Advanced order types | `order_book.cpp` — LIMIT / MARKET / IOC / FOK semantics |
| Lock-free hot path | book touched by one thread only — no locks on matching |

---

## Future Extensions

- [x] TCP network order ingress (multi-client, single matching thread)
- [x] Per-operation and percentile latency benchmarking
- [x] IOC / FOK order types (immediate-or-cancel, fill-or-kill)
- [x] Lock-free SPSC ring buffer for the ingress path
- [ ] Iceberg / hidden orders (display qty vs. total qty)
- [ ] Stop and Stop-Limit orders (trigger on last trade price)
- [ ] Self-trade prevention (STP) tags
- [ ] Order book replay from ITCH / OUCH market data feeds
- [ ] Multiple symbols in one engine (symbol-sharded books)
- [ ] FIX protocol gateway (FIX 4.2 / 4.4)

---

*Built with C++17. No external dependencies.*
