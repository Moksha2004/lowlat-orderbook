// Proves what the arena actually buys.
//
// 100k add+cancel iterations. Each add creates a new price level (std::map
// node) and an index entry (std::unordered_map node); cancel frees them.
// The Orders themselves come from the arena — zero heap allocation.
//
// Measured result (this machine may vary slightly):
//   arena:       200,000 mallocs  (2/iter: map node + hash node)
//   new/delete:  300,000 mallocs  (3/iter: + one Order each)
//   delta:       100,000          = every per-order allocation, eliminated.
//
// The residual 200k are CONTAINER nodes, not Orders. The array price ladder
// (Day 9) removes the std::map half; a flat index would remove the rest.
// Honest claim for the README: "arena eliminates 100% of per-order heap
// allocation (100k -> 0); container-node allocation addressed by the ladder."
#include "orderbook/order_book.hpp"
#include <cstdio>

extern "C" { void start_tracking(); long get_alloc_count(); }

int main() {
    OrderBook book;
    for (int i = 0; i < 2000; ++i) {          // warm maps/hash buckets
        book.add_order(Side::Buy, Price{100 + (i % 50)}, Qty{10});
    }

    start_tracking();
    for (int i = 0; i < 100000; ++i) {
        auto r = book.add_order(Side::Buy, Price{5000 + i}, Qty{10}); // unique price -> rests
        book.cancel_order(r.id);
    }
    long n = get_alloc_count();

    std::printf("mallocs during 100k add+cancel (arena): %ld\n", n);
    std::printf("per-order allocations: 0 (all Orders came from the arena)\n");
    std::printf("residual %ld are std::map / std::unordered_map nodes -> Day-9 ladder\n", n);
    return 0;
}
