#include "gateway.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace orderbook {

namespace {

// Trim leading and trailing whitespace from a string.
std::string trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start))) {
        ++start;
    }
    auto end = s.end();
    while (end != start && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(start, end);
}

// Split a string by a delimiter and trim each token.
std::vector<std::string> splitAndTrim(const std::string& line, char delim) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;
    while (std::getline(stream, token, delim)) {
        tokens.push_back(trim(token));
    }
    return tokens;
}

} // anonymous namespace

Gateway::Message Gateway::parseLine(const std::string& line) {
    Message msg;

    // Trim the input line.
    std::string trimmed = trim(line);

    // Empty lines and comment lines are INVALID.
    if (trimmed.empty() || trimmed[0] == '#') {
        return msg;
    }

    try {
        std::vector<std::string> tokens = splitAndTrim(trimmed, ',');
        if (tokens.empty()) {
            return msg;
        }

        const std::string& cmd = tokens[0];

        if (cmd == "N") {
            // N,<id>,<side>,<type>,<price>,<qty>
            if (tokens.size() < 6) {
                return msg;
            }

            OrderId id = std::stoull(tokens[1]);

            Side side;
            if (tokens[2] == "BID") {
                side = Side::BID;
            } else if (tokens[2] == "ASK") {
                side = Side::ASK;
            } else {
                return msg;
            }

            OrderType orderType;
            if (tokens[3] == "LIMIT") {
                orderType = OrderType::LIMIT;
            } else if (tokens[3] == "MARKET") {
                orderType = OrderType::MARKET;
            } else if (tokens[3] == "IOC") {
                orderType = OrderType::IOC;
            } else if (tokens[3] == "FOK") {
                orderType = OrderType::FOK;
            } else {
                return msg;
            }

            double priceDouble = std::stod(tokens[4]);
            Ticks price = priceToTicks(priceDouble);
            // For MARKET orders, price may be 0 or ignored.
            if (orderType == OrderType::MARKET) {
                price = 0;
            }

            Quantity qty = static_cast<Quantity>(std::stoi(tokens[5]));

            Order order{};
            order.id = id;
            order.side = side;
            order.type = orderType;
            order.price = price;
            order.quantity = qty;
            order.remaining = qty;

            msg.type = Message::Type::NEW;
            msg.order = order;

        } else if (cmd == "C") {
            // C,<id>
            if (tokens.size() < 2) {
                return msg;
            }

            msg.type = Message::Type::CANCEL;
            msg.target_id = std::stoull(tokens[1]);

        } else if (cmd == "M") {
            // M,<id>,<qty>,<price>
            if (tokens.size() < 4) {
                return msg;
            }

            msg.type = Message::Type::MODIFY;
            msg.target_id = std::stoull(tokens[1]);
            msg.new_quantity = static_cast<Quantity>(std::stoi(tokens[2]));
            msg.new_price = priceToTicks(std::stod(tokens[3]));
        }
        // Unknown command: msg stays INVALID.

    } catch (...) {
        // Any parsing exception results in INVALID.
        msg.type = Message::Type::INVALID;
    }

    return msg;
}

} // namespace orderbook
