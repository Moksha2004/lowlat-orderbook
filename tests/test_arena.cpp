#include <gtest/gtest.h>
#include "orderbook/arena_allocator.hpp"   // pulls in the REAL Order, not a mock

TEST(ArenaTest, AllocationAndExhaustion) {
    OrderArena arena(3);
    EXPECT_EQ(arena.capacity(), 3u);
    EXPECT_EQ(arena.available(), 3u);

    Order* o1 = arena.allocate();
    Order* o2 = arena.allocate();
    Order* o3 = arena.allocate();
    EXPECT_NE(o1, nullptr);
    EXPECT_NE(o2, nullptr);
    EXPECT_NE(o3, nullptr);
    EXPECT_EQ(arena.available(), 0u);

    // Exhausted → strict reject.
    EXPECT_EQ(arena.allocate(), nullptr);
}

TEST(ArenaTest, FreeAndRecycle) {
    OrderArena arena(2);
    Order* o1 = arena.allocate();
    Order* o2 = arena.allocate();
    o1->id = OrderID{100};
    o2->id = OrderID{200};

    // LIFO: free o2 then o1 → free_head_ = o1.
    arena.free(o2);
    arena.free(o1);
    EXPECT_EQ(arena.available(), 2u);

    // Pop returns o1 first, then o2 — same slots, data preserved
    // (free/allocate only touch `next`).
    Order* r1 = arena.allocate();
    Order* r2 = arena.allocate();
    EXPECT_EQ(r1->id.v, 100u);
    EXPECT_EQ(r2->id.v, 200u);
    EXPECT_EQ(arena.available(), 0u);
}

TEST(ArenaTest, ZeroCapacityIsSafe) {
    OrderArena arena(0);
    EXPECT_EQ(arena.available(), 0u);
    EXPECT_EQ(arena.allocate(), nullptr);
}
