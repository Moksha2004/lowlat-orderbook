#pragma once
#include "orderbook/types.hpp"
#include <cstdint>

// One price level = FIFO queue of resting orders at a single price.
// Intrusive: Orders ARE the nodes (Order::prev/next). This class never
// allocates or frees anything — it only rewires pointers it doesn't own.
//
// Implementations are in-class (implicitly inline): push_back/remove are
// hot-path and must be inlinable into the matching loop without LTO.
class PriceLevel {
    Order*   head_{nullptr};   // oldest order — first to match
    Order*   tail_{nullptr};   // newest order — last to match
    uint32_t count_{0};
    uint64_t total_qty_{0};    // sum of resting qty; book uses this for depth

public:
    // Append to tail (new orders go to the back — that IS time priority).
    // Pre: o is not in any list (o->prev == o->next == nullptr).
    void push_back(Order* o) {
        // 1. Set up the new order's links (it goes to the end)
        o->prev = tail_;
        o->next = nullptr;
        // 2. Link the current tail to the new order, or set head if empty
        if (tail_) {
            tail_->next = o;
        } else {
            head_ = o;
        }
        // 3. Update the tail pointer and stats
        tail_ = o;
        count_++;
        total_qty_ += o->qty.v;
    }

    // Unlink o from this level. O(1) — no walking, no searching.
    // Works because the cancel path already has Order* from the
    // OrderID hash map (SPEC §5).
    void remove(Order* o) {
        // Does o have a prev?
        if (o->prev) {
            o->prev->next = o->next; // middle/tail case
        } else {
            head_ = o->next;         // head case
        }
        // Does o have a next?
        if (o->next) {
            o->next->prev = o->prev; // middle/head case
        } else {
            tail_ = o->prev;         // tail case
        }
        // Update stats
        count_--;
        total_qty_ -= o->qty.v;
        // Scrub the pointers on the removed order — a stale prev/next here
        // becomes a use-after-free when the arena recycles this slot.
        o->prev = nullptr;
        o->next = nullptr;
    }

    Order*   front()     const { return head_; }
    bool     empty()     const { return head_ == nullptr; }
    uint32_t count()     const { return count_; }
    uint64_t total_qty() const { return total_qty_; }
};
