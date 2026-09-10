#include "order_server.h"
#include "gateway.h"
#include "types.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <chrono>

namespace orderbook {

namespace {
void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
}  // namespace

OrderServer::OrderServer(int port, const std::string& symbol)
    : port_(port),
      listen_fd_(-1),
      running_(false),
      book_(symbol, 1u << 16),   // reserve order index to avoid rehash spikes
      queue_(1u << 16) {}        // 65,536-slot lock-free ingress ring

OrderServer::~OrderServer() {
    stop();
}

void OrderServer::start() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw std::runtime_error("Failed to create socket");

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(listen_fd_);
        throw std::runtime_error("Failed to bind to port " + std::to_string(port_));
    }
    if (listen(listen_fd_, SOMAXCONN) < 0) {
        close(listen_fd_);
        throw std::runtime_error("Failed to listen");
    }
    setNonBlocking(listen_fd_);

    running_.store(true, std::memory_order_release);

    // Start the single matching thread (the sole consumer of the SPSC queue).
    matching_thread_ = std::thread(&OrderServer::matchingLoop, this);

    std::cout << "[order-book] Matching engine listening on port " << port_
              << " (symbol " << book_.symbol() << ")\n";
    std::cout << "[order-book] Architecture: 1 I/O thread -> lock-free SPSC queue -> 1 matching thread\n";
    std::cout << "[order-book] Orders: N,id,side,type,price,qty | C,id | M,id,qty,price"
              << "   (type = LIMIT|MARKET|IOC|FOK)" << std::endl;

    ioLoop();  // blocks in the calling thread until stop()

    if (matching_thread_.joinable()) matching_thread_.join();
}

void OrderServer::stop() {
    running_.store(false, std::memory_order_release);
    // ioLoop() wakes from poll() within its timeout and returns; the matching
    // thread observes running_ == false and drains, then exits.
}

// PRODUCER: a single thread polls the listen socket + all client sockets,
// reads available bytes, splits them into newline-delimited order lines, and
// pushes each into the SPSC queue. Being the ONLY producer satisfies the SPSC
// contract.
void OrderServer::ioLoop() {
    std::vector<pollfd> fds;
    fds.push_back(pollfd{listen_fd_, POLLIN, 0});

    std::unordered_map<int, std::string> bufs;  // per-connection read buffer

    while (running_.load(std::memory_order_acquire)) {
        int ret = ::poll(fds.data(), fds.size(), 200 /*ms*/);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;  // timeout: loop to re-check running_

        // New connections on the listen socket.
        if (fds[0].revents & POLLIN) {
            int cfd;
            while ((cfd = accept(listen_fd_, nullptr, nullptr)) >= 0) {
                int one = 1;
                setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                setNonBlocking(cfd);
                fds.push_back(pollfd{cfd, POLLIN, 0});
                bufs[cfd] = std::string();
            }
        }

        // Client sockets (index 0 is the listen socket).
        for (std::size_t i = 1; i < fds.size();) {
            const short re = fds[i].revents;
            const int cfd = fds[i].fd;
            bool remove = false;

            if (re & (POLLHUP | POLLERR | POLLNVAL)) {
                remove = true;
            } else if (re & POLLIN) {
                char rbuf[4096];
                ssize_t n;
                while ((n = recv(cfd, rbuf, sizeof(rbuf), 0)) > 0) {
                    bufs[cfd].append(rbuf, static_cast<size_t>(n));
                }
                if (n == 0) {
                    remove = true;  // peer closed
                } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    remove = true;  // real error
                }

                // Extract complete order lines and enqueue them.
                std::string& b = bufs[cfd];
                std::size_t pos;
                while ((pos = b.find('\n')) != std::string::npos) {
                    std::string line = b.substr(0, pos);
                    b.erase(0, pos + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty()) continue;

                    // Single-producer push. If the ring is momentarily full,
                    // back off briefly rather than dropping the order.
                    Job job{std::move(line), cfd};
                    while (!queue_.push(std::move(job))) {
                        if (!running_.load(std::memory_order_acquire)) break;
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                    }
                }
            }

            if (remove) {
                close(cfd);
                bufs.erase(cfd);
                fds.erase(fds.begin() + static_cast<long>(i));
            } else {
                ++i;
            }
        }
    }

    // Shutdown: close all sockets.
    for (auto& p : fds) {
        if (p.fd >= 0) close(p.fd);
    }
    listen_fd_ = -1;
}

// CONSUMER: the single matching thread. It is the ONLY thread that touches the
// OrderBook, so the matching hot path needs no locks.
void OrderServer::matchingLoop() {
    Job job;
    while (running_.load(std::memory_order_acquire)) {
        if (queue_.pop(job)) {
            std::string resp = process(job.line) + "\n";
            send(job.client_fd, resp.c_str(), resp.size(), 0);
        } else {
            // Queue empty: brief idle backoff so we don't peg a core at 100%.
            // A production engine pinned to a dedicated core would busy-spin here.
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }
    // Drain anything still queued at shutdown.
    while (queue_.pop(job)) {
        std::string resp = process(job.line) + "\n";
        send(job.client_fd, resp.c_str(), resp.size(), 0);
    }
}

std::string OrderServer::process(const std::string& line) {
    Gateway::Message msg = Gateway::parseLine(line);
    std::ostringstream out;

    switch (msg.type) {
        case Gateway::Message::Type::NEW: {
            auto trades = book_.addOrder(msg.order);
            Quantity filled = 0;
            for (const auto& t : trades) {
                analytics_.recordTrade(t);
                filled += t.quantity;
            }
            out << "OK NEW id=" << msg.order.id
                << " type=" << orderTypeToString(msg.order.type)
                << " trades=" << trades.size()
                << " filled=" << filled;
            auto bb = book_.bestBid();
            auto ba = book_.bestAsk();
            out << " bid=" << (bb ? std::to_string(ticksToPrice(*bb)) : "-")
                << " ask=" << (ba ? std::to_string(ticksToPrice(*ba)) : "-");
            break;
        }
        case Gateway::Message::Type::CANCEL: {
            bool removed = book_.cancelOrder(msg.target_id);
            out << "OK CANCEL id=" << msg.target_id << " removed=" << (removed ? 1 : 0);
            break;
        }
        case Gateway::Message::Type::MODIFY: {
            auto trades = book_.modifyOrder(msg.target_id, msg.new_quantity, msg.new_price);
            for (const auto& t : trades) analytics_.recordTrade(t);
            out << "OK MODIFY id=" << msg.target_id << " trades=" << trades.size();
            break;
        }
        case Gateway::Message::Type::INVALID:
        default:
            out << "ERR invalid order: " << line;
            break;
    }

    return out.str();
}

} // namespace orderbook
