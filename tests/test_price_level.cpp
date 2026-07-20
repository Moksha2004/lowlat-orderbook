#include <gtest/gtest.h>
#include "orderbook/price_level.hpp"
#include "orderbook/types.hpp"

// Helper to stamp out orders without bloating the cases.
// NOTE: designated initializers must follow declaration order
// (next, prev, price, id, qty, participant_id, side), and the qty
// parameter is uint32_t — Qty{uint64_t} would be a narrowing error.
static Order make_test_order(uint32_t qty) {
    return Order{
        .next = nullptr,
        .prev = nullptr,
        .price = Price{100},
        .id = OrderID{1},
        .qty = Qty{qty},
        .participant_id = ParticipantID{1},
        .side = Side::Buy
    };
}

TEST(PriceLevelTest, PushBack_EmptyThenThree) {
    PriceLevel level;
    Order a = make_test_order(10);
    Order b = make_test_order(20);
    Order c = make_test_order(30);
    level.push_back(&a);
    level.push_back(&b);
    level.push_back(&c);
    EXPECT_EQ(level.front(), &a);
    EXPECT_EQ(level.count(), 3u);
    EXPECT_EQ(level.total_qty(), 60u);
}

TEST(PriceLevelTest, Remove_OnlyElement) {
    PriceLevel level;
    Order a = make_test_order(10);
    level.push_back(&a);

    level.remove(&a);

    EXPECT_TRUE(level.empty());
    EXPECT_EQ(level.front(), nullptr);
    EXPECT_EQ(level.count(), 0u);
    EXPECT_EQ(level.total_qty(), 0u);
}

TEST(PriceLevelTest, Remove_Head) {
    PriceLevel level;
    Order a = make_test_order(10);
    Order b = make_test_order(20);
    level.push_back(&a);
    level.push_back(&b);

    level.remove(&a);

    EXPECT_EQ(level.front(), &b);
    EXPECT_EQ(level.count(), 1u);
    EXPECT_EQ(level.total_qty(), 20u);
    EXPECT_EQ(b.prev, nullptr);
}

TEST(PriceLevelTest, Remove_Tail) {
    PriceLevel level;
    Order a = make_test_order(10);
    Order b = make_test_order(20);
    level.push_back(&a);
    level.push_back(&b);

    level.remove(&b);

    EXPECT_EQ(level.front(), &a);
    EXPECT_EQ(level.count(), 1u);
    EXPECT_EQ(level.total_qty(), 10u);
    EXPECT_EQ(a.next, nullptr);

    // Push a new order after removal — catches a stale tail_.
    Order c = make_test_order(30);
    level.push_back(&c);

    EXPECT_EQ(a.next, &c);
    EXPECT_EQ(c.prev, &a);
    EXPECT_EQ(level.count(), 2u);
    EXPECT_EQ(level.total_qty(), 40u);
}

TEST(PriceLevelTest, Remove_Middle) {
    PriceLevel level;
    Order a = make_test_order(10);
    Order b = make_test_order(20);
    Order c = make_test_order(30);

    level.push_back(&a);
    level.push_back(&b);
    level.push_back(&c);

    level.remove(&b);

    EXPECT_EQ(level.count(), 2u);
    EXPECT_EQ(level.total_qty(), 40u);

    // Walk forward
    Order* curr = level.front();
    EXPECT_EQ(curr, &a);
    curr = curr->next;
    EXPECT_EQ(curr, &c);
    EXPECT_EQ(curr->next, nullptr);

    // Walk backward (verifies C points back to A, not B)
    EXPECT_EQ(c.prev, &a);
    EXPECT_EQ(a.prev, nullptr);
}

TEST(PriceLevelTest, RemovedOrder_IsClean) {
    PriceLevel level;
    Order a = make_test_order(10);
    Order b = make_test_order(20);
    Order c = make_test_order(30);

    level.push_back(&a);
    level.push_back(&b);
    level.push_back(&c);

    level.remove(&b);

    EXPECT_EQ(b.prev, nullptr);
    EXPECT_EQ(b.next, nullptr);
}
