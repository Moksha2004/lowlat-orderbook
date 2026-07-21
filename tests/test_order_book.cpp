#include <gtest/gtest.h>
#include "orderbook/order_book.hpp"

TEST(OrderBookTest, TopOfBookAndCancelBehavior) {
    OrderBook book;

    // 1. Empty book.
    auto top = book.top_of_book();
    EXPECT_FALSE(top.has_bid);
    EXPECT_FALSE(top.has_ask);

    // 2. Build the book.
    auto id1 = book.add_order(Side::Buy,  Price{100}, Qty{10}, ParticipantID{1});
    auto id2 = book.add_order(Side::Buy,  Price{100}, Qty{5},  ParticipantID{2});
    (void) book.add_order(Side::Buy,      Price{99},  Qty{20}, ParticipantID{3});
    (void) book.add_order(Side::Sell,     Price{102}, Qty{50}, ParticipantID{4});

    top = book.top_of_book();
    EXPECT_TRUE(top.has_bid);
    EXPECT_EQ(top.best_bid.v, 100);
    EXPECT_EQ(top.bid_qty, 15u);        // id1 + id2 at the 100 level
    EXPECT_TRUE(top.has_ask);
    EXPECT_EQ(top.best_ask.v, 102);
    EXPECT_EQ(top.ask_qty, 50u);

    // 3. Cancel the best order — top stays at 100, qty drops to just id2.
    EXPECT_TRUE(book.cancel_order(id1));
    top = book.top_of_book();
    EXPECT_EQ(top.best_bid.v, 100);
    EXPECT_EQ(top.bid_qty, 5u);

    // 4. Cancel the last order at 100 — the level is erased, top drops to 99.
    EXPECT_TRUE(book.cancel_order(id2));
    top = book.top_of_book();
    EXPECT_EQ(top.best_bid.v, 99);
    EXPECT_EQ(top.bid_qty, 20u);

    // 5. Cancelling a non-existent id fails cleanly.
    EXPECT_FALSE(book.cancel_order(OrderID{999}));
}
