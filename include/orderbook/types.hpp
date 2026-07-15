#pragma once
#include <cstdint>
#include <compare>
#include <type_traits>

struct Price {
    int64_t v;
    constexpr explicit Price(int64_t x) : v(x) {}
    auto operator<=>(const Price&) const = default;
};

struct Qty {
    uint32_t v;
    constexpr explicit Qty(uint32_t x) : v(x) {}
    auto operator<=>(const Qty&) const = default;
};

struct OrderID {
    uint64_t v;
    constexpr explicit OrderID(uint64_t x) : v(x) {}
    auto operator<=>(const OrderID&) const = default;
};

struct ParticipantID {
    uint32_t v;
    constexpr explicit ParticipantID(uint32_t x) : v(x) {}
    auto operator<=>(const ParticipantID&) const = default;
};

enum class Side : uint8_t { Buy, Sell };

struct Order {
    // Intrusive pointers first
    Order* next{nullptr};          // 8 bytes
    Order* prev{nullptr};          // 8 bytes

    // Hot path matching fields
    Price price;                   // 8 bytes
    OrderID id;                    // 8 bytes
    Qty qty;                       // 4 bytes
    ParticipantID participant_id;  // 4 bytes
    Side side;                     // 1 byte

    // 7 bytes of implicit compiler padding to reach 8-byte alignment
};

// Compile-time layout guards — fire in every TU that includes this header,
// not just when the test target rebuilds.
static_assert(sizeof(Order) == 48, "Order grew — check padding before accepting this");
static_assert(sizeof(Order) <= 64, "Order must fit one cache line");

// The arena (Day 5) recycles Order slots without running destructors.
// Only legal while Order is trivially destructible — this turns a silent
// leak (someone adds a std::string field) into a loud compile error.
static_assert(std::is_trivially_destructible_v<Order>);
