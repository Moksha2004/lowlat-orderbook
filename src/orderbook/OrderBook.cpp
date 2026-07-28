#include "OrderBook.hpp"

Status OrderBook::add_order(uint64_t id, uint64_t price, uint32_t qty, Side side) {
    // 1. Match loop execution here...
    // If fully filled aggressively against resting orders, return Status::OK without allocating.

    // 2. Resting the order
    Order* o = arena_.allocate();
    if (!o) [[unlikely]] {
        return Status::REJECT_CAPACITY;
    }

    // 3. Copy fields
    o->id = id;
    o->price = price;
    o->qty = qty;
    o->side = side;

    // 4. Link into index and price levels
    index_[id] = o;
    // ... insert into PriceLevel linked list ...

    return Status::OK;
}

void OrderBook::cancel_order(uint64_t id) {
    auto it = index_.find(id);
    if (it == index_.end()) return;

    Order* o = it->second;

    // 1. Unlink from PriceLevel linked list
    // ...

    // 2. Erase from index
    index_.erase(it);

    // 3. Recycle memory immediately
    arena_.free(o);
}

// Inside the match loop (when an incoming order completely fills a resting order):
/*
if (resting_order->qty == 0) {
    unlink_from_price_level(resting_order);
    index_.erase(resting_order->id);
    arena_.free(resting_order); // Recycle slot
}
*/
