#include "orderbook/price_level.hpp"

void PriceLevel::push_back(Order* o) {
    // 1. Setup the new order's links (it goes to the end)
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

void PriceLevel::remove(Order* o) {
    // Does o have a prev?
    if (o->prev) {
        o->prev->next = o->next; // Middle/Tail case
    } else {
        head_ = o->next;         // Head case
    }

    // Does o have a next?
    if (o->next) {
        o->next->prev = o->prev; // Middle/Head case
    } else {
        tail_ = o->prev;         // Tail case
    }

    // Update stats
    count_--;
    total_qty_ -= o->qty.v;

    // Scrub the pointers on the removed order
    o->prev = nullptr;
    o->next = nullptr;
}