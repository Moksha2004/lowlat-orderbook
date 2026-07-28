#pragma once
#include "arena_allocator.hpp"
#include <unordered_map>
#include <cstdint>

enum class Side { Buy, Sell };
enum class Status { OK, REJECT_CAPACITY };

class OrderBook {
private:
    OrderArena arena_{1'000'000}; 
    std::unordered_map<uint64_t, Order*> index_;
    // ... price levels and other members ...

public:
    Status add_order(uint64_t id, uint64_t price, uint32_t qty, Side side);
    void cancel_order(uint64_t id);
    // ...
};
