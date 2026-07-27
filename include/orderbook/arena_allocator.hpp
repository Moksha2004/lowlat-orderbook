#pragma once
#include "orderbook/types.hpp"   // real Order — test against this, never a mock
#include <vector>
#include <cstddef>

// Fixed-capacity pool of Order slots with an intrusive free list.
// One heap allocation (the pool) in the constructor; allocate()/free() are
// O(1) and allocation-free thereafter — SPEC invariant #2.
//
// The free list costs zero extra memory: a free slot isn't holding a live
// order, so its Order::next is unused — we thread the free list through it.
// A slot is EITHER free (linked via next in this list) OR live (linked via
// next inside a PriceLevel), never both, so next has exactly one owner at any
// instant.
class OrderArena {
    std::vector<Order> pool_;
    Order*             free_head_{nullptr};
    size_t             capacity_{0};
    size_t             available_{0};

public:
    explicit OrderArena(size_t capacity)
        : capacity_(capacity), available_(capacity)
    {
        if (capacity_ == 0) return;

        pool_.resize(capacity_);   // the ONE allocation

        // Link every slot to the next; last terminates the list.
        for (size_t i = 0; i + 1 < capacity_; ++i) {
            pool_[i].next = &pool_[i + 1];
        }
        pool_[capacity_ - 1].next = nullptr;
        free_head_ = &pool_[0];
    }

    // Pop a slot off the free list. nullptr when exhausted (strict reject —
    // growing the pool would relocate the vector and invalidate every live
    // Order* in the levels and index).
    Order* allocate() {
        if (!free_head_) [[unlikely]] return nullptr;
        Order* o = free_head_;
        free_head_ = free_head_->next;
        o->next = nullptr;         // sever so the live order carries no stale link
        --available_;
        return o;
    }

    // Push a slot back onto the free list (LIFO).
    void free(Order* o) {
        o->next = free_head_;
        free_head_ = o;
        ++available_;
    }

    size_t capacity()  const { return capacity_; }
    size_t available() const { return available_; }
};
