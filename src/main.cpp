#include "order_book.h"
#include "gateway.h"
#include "market_data.h"
#include "analytics.h"
#include "order_server.h"
#include "types.h"

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <csignal>

using namespace orderbook;

static OrderServer* g_server = nullptr;

static void signalHandler(int) {
    if (g_server) g_server->stop();
}

int main(int argc, char* argv[]) {
    std::string symbol = "AAPL";
    std::string feed_file;
    bool verbose = false;
    bool serve = false;
    int port = 9001;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-s" && i + 1 < argc) symbol = argv[++i];
        else if (arg == "-f" && i + 1 < argc) feed_file = argv[++i];
        else if (arg == "-v") verbose = true;
        else if (arg == "--serve") serve = true;
        else if (arg == "-p" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [-s SYMBOL] [-f FEED_FILE] [-v] [--serve -p PORT]\n"
                      << "  -s SYMBOL   Instrument symbol (default AAPL)\n"
                      << "  -f FILE     Order feed file (CSV). If omitted, reads stdin.\n"
                      << "  -v          Verbose: print each trade and L2 snapshots\n"
                      << "  --serve     Run as a TCP matching-engine server (network ingress)\n"
                      << "  -p PORT     Port for --serve mode (default 9001)\n";
            return 0;
        }
    }

    // ---- Network mode: run as a live matching-engine server ----
    if (serve) {
        OrderServer server(port, symbol);
        g_server = &server;
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);
        try {
            server.start();  // blocking accept loop
        } catch (const std::exception& e) {
            std::cerr << "[order-book] Fatal: " << e.what() << std::endl;
            return 1;
        }
        std::cout << "[order-book] Server stopped." << std::endl;
        return 0;
    }

    // ---- Batch mode: replay orders from a file or stdin ----
    OrderBook book(symbol);
    Analytics analytics;

    std::istream* in = &std::cin;
    std::ifstream file;
    if (!feed_file.empty()) {
        file.open(feed_file);
        if (!file) { std::cerr << "Cannot open feed file: " << feed_file << std::endl; return 1; }
        in = &file;
    }

    auto start = std::chrono::high_resolution_clock::now();
    uint64_t lines = 0;

    std::string line;
    while (std::getline(*in, line)) {
        if (line.empty()) continue;
        Gateway::Message msg = Gateway::parseLine(line);
        ++lines;

        std::vector<Trade> trades;
        switch (msg.type) {
            case Gateway::Message::Type::NEW:
                trades = book.addOrder(msg.order);
                break;
            case Gateway::Message::Type::CANCEL:
                book.cancelOrder(msg.target_id);
                break;
            case Gateway::Message::Type::MODIFY:
                trades = book.modifyOrder(msg.target_id, msg.new_quantity, msg.new_price);
                break;
            case Gateway::Message::Type::INVALID:
            default:
                continue;
        }

        for (const auto& t : trades) {
            analytics.recordTrade(t);
            if (verbose) {
                std::cout << "TRADE  " << t.quantity << " @ " << ticksToPrice(t.price)
                          << "  (buy #" << t.buy_order_id << " / sell #" << t.sell_order_id << ")\n";
            }
        }
        if (verbose && msg.type == Gateway::Message::Type::NEW) {
            auto snap = MarketData::snapshot(book, 5);
            std::cout << MarketData::formatL2(snap) << "\n";
        }
    }

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

    std::cout << "\n=== Order Book Summary (" << symbol << ") ===\n";
    std::cout << "Feed lines processed: " << lines << "\n";
    std::cout << "Total orders:         " << book.totalOrders() << "\n";
    std::cout << "Total trades:         " << book.totalTrades() << "\n";
    std::cout << "Total cancels:        " << book.totalCancels() << "\n";
    std::cout << "Resting orders:       " << book.restingOrderCount() << "\n";
    std::cout << "Bid levels:           " << book.bidLevels() << "\n";
    std::cout << "Ask levels:           " << book.askLevels() << "\n";
    if (ns > 0 && lines > 0) {
        double per_op_ns = static_cast<double>(ns) / static_cast<double>(lines);
        std::cout << "Processing time:      " << (ns / 1e6) << " ms\n";
        std::cout << "Avg latency:          " << per_op_ns << " ns/op\n";
        std::cout << "Throughput:           " << (1e9 / per_op_ns) << " ops/sec\n";
    }
    std::cout << "\n" << analytics.summary() << "\n";

    // Final book snapshot
    auto snap = MarketData::snapshot(book, 5);
    std::cout << MarketData::formatL2(snap) << std::endl;

    return 0;
}
