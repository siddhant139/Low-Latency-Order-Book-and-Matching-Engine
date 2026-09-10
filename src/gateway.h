#pragma once

#include "types.h"
#include <string>
#include <optional>

namespace orderbook {

// Parses a text order feed into structured messages.
// Line formats (comma-separated):
//   N,<id>,<side>,<type>,<price>,<qty>   New order   e.g. N,1,BID,LIMIT,105.25,100
//   C,<id>                               Cancel      e.g. C,1
//   M,<id>,<qty>,<price>                 Modify      e.g. M,1,150,105.50
// side = BID|ASK ; type = LIMIT|MARKET ; price in dollars (converted to ticks)
class Gateway {
public:
    struct Message {
        enum class Type { NEW, CANCEL, MODIFY, INVALID } type = Type::INVALID;
        Order order;              // for NEW
        OrderId target_id = 0;    // for CANCEL / MODIFY
        Quantity new_quantity = 0;// for MODIFY
        Ticks new_price = 0;      // for MODIFY
    };

    // Parse a single line into a Message. type == INVALID on parse failure.
    static Message parseLine(const std::string& line);
};

} // namespace orderbook
