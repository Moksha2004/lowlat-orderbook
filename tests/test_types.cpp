#include <gtest/gtest.h>
#include "orderbook/types.hpp"
// Size/layout checks live in types.hpp as static_asserts — compile-time,
// enforced everywhere. This file keeps the runtime behavior tests only.

TEST(TypesTest, StrongTypedefComparisons) {
    Price p1{10050};
    Price p2{10050};
    Price p3{10051};
    EXPECT_EQ(p1, p2);
    EXPECT_LT(p1, p3);
    EXPECT_GT(p3, p1);

    Qty q1{50};
    Qty q2{100};
    EXPECT_NE(q1, q2);
    EXPECT_LT(q1, q2);
}

TEST(TypesTest, OrderFieldAccess) {
    Order o{
        .next = nullptr,
        .prev = nullptr,
        .price = Price{25000},
        .id = OrderID{1042},
        .qty = Qty{500},
        .participant_id = ParticipantID{7},
        .side = Side::Sell
    };
    EXPECT_EQ(o.price.v, 25000);
    EXPECT_EQ(o.qty.v, 500);
    EXPECT_EQ(o.side, Side::Sell);
    EXPECT_EQ(o.id.v, 1042);
    EXPECT_EQ(o.next, nullptr);
}
