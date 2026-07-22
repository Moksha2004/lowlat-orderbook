#include <gtest/gtest.h>
#include "orderbook/order_book.hpp"

TEST(OrderBookTest, TopOfBookAndCancelBehavior) {
    OrderBook book;

    auto top = book.top_of_book();
    EXPECT_FALSE(top.has_bid);
    EXPECT_FALSE(top.has_ask);

    auto id1 = book.add_order(Side::Buy,  Price{100}, Qty{10}, ParticipantID{1});
    auto id2 = book.add_order(Side::Buy,  Price{100}, Qty{5},  ParticipantID{2});
    (void) book.add_order(Side::Buy,      Price{99},  Qty{20}, ParticipantID{3});
    (void) book.add_order(Side::Sell,     Price{102}, Qty{50}, ParticipantID{4});

    top = book.top_of_book();
    EXPECT_EQ(top.best_bid.v, 100);
    EXPECT_EQ(top.bid_qty, 15u);
    EXPECT_EQ(top.best_ask.v, 102);
    EXPECT_EQ(top.ask_qty, 50u);

    EXPECT_TRUE(book.cancel_order(id1.id));
    top = book.top_of_book();
    EXPECT_EQ(top.best_bid.v, 100);
    EXPECT_EQ(top.bid_qty, 5u);

    EXPECT_TRUE(book.cancel_order(id2.id));
    top = book.top_of_book();
    EXPECT_EQ(top.best_bid.v, 99);
    EXPECT_EQ(top.bid_qty, 20u);

    EXPECT_FALSE(book.cancel_order(OrderID{999}));
}

TEST(OrderBookTest, Match_NoCrossRests) {
    OrderBook book;
    auto res1 = book.add_order(Side::Buy, Price{100}, Qty{10});
    auto res2 = book.add_order(Side::Sell, Price{105}, Qty{10});

    EXPECT_TRUE(res1.fills.empty());
    EXPECT_TRUE(res2.fills.empty());

    auto top = book.top_of_book();
    EXPECT_EQ(top.best_bid.v, 100);
    EXPECT_EQ(top.best_ask.v, 105);
}

TEST(OrderBookTest, Match_FullFill) {
    OrderBook book;
    auto res_rest = book.add_order(Side::Sell, Price{100}, Qty{10});
    auto res_agg = book.add_order(Side::Buy, Price{100}, Qty{10});

    EXPECT_EQ(res_agg.fills.size(), 1u);
    EXPECT_EQ(res_agg.fills[0].price.v, 100);
    EXPECT_EQ(res_agg.fills[0].qty.v, 10u);
    EXPECT_EQ(res_agg.fills[0].aggressor.v, res_agg.id.v);
    EXPECT_EQ(res_agg.fills[0].resting.v, res_rest.id.v);

    auto top = book.top_of_book();
    EXPECT_FALSE(top.has_ask);
}

TEST(OrderBookTest, Match_PartialFillLeavesResting) {
    OrderBook book;
    (void) book.add_order(Side::Sell, Price{100}, Qty{20});
    auto res_agg = book.add_order(Side::Buy, Price{102}, Qty{5});

    EXPECT_EQ(res_agg.fills.size(), 1u);
    EXPECT_EQ(res_agg.fills[0].price.v, 100);
    EXPECT_EQ(res_agg.fills[0].qty.v, 5u);

    auto top = book.top_of_book();
    EXPECT_TRUE(top.has_ask);
    EXPECT_EQ(top.best_ask.v, 100);
    EXPECT_EQ(top.ask_qty, 15u);
}

TEST(OrderBookTest, Match_PartialFillLeavesAggressor) {
    OrderBook book;
    (void) book.add_order(Side::Sell, Price{100}, Qty{5});
    auto res_agg = book.add_order(Side::Buy, Price{100}, Qty{20});

    EXPECT_EQ(res_agg.fills.size(), 1u);
    EXPECT_EQ(res_agg.fills[0].qty.v, 5u);

    auto top = book.top_of_book();
    EXPECT_FALSE(top.has_ask);
    EXPECT_TRUE(top.has_bid);
    EXPECT_EQ(top.best_bid.v, 100);
    EXPECT_EQ(top.bid_qty, 15u);
}

TEST(OrderBookTest, Match_SweepMultipleLevels) {
    OrderBook book;
    auto ask1 = book.add_order(Side::Sell, Price{100}, Qty{10});
    auto ask2 = book.add_order(Side::Sell, Price{101}, Qty{10});

    auto res_agg = book.add_order(Side::Buy, Price{105}, Qty{15});

    EXPECT_EQ(res_agg.fills.size(), 2u);
    EXPECT_EQ(res_agg.fills[0].price.v, 100);
    EXPECT_EQ(res_agg.fills[0].qty.v, 10u);
    EXPECT_EQ(res_agg.fills[0].resting.v, ask1.id.v);
    EXPECT_EQ(res_agg.fills[1].price.v, 101);
    EXPECT_EQ(res_agg.fills[1].qty.v, 5u);
    EXPECT_EQ(res_agg.fills[1].resting.v, ask2.id.v);

    auto top = book.top_of_book();
    EXPECT_EQ(top.best_ask.v, 101);
    EXPECT_EQ(top.ask_qty, 5u);
}

TEST(OrderBookTest, Match_ExactMultiOrderFillAtOneLevel) {
    OrderBook book;
    auto ask1 = book.add_order(Side::Sell, Price{100}, Qty{10});
    auto ask2 = book.add_order(Side::Sell, Price{100}, Qty{5});

    auto res_agg = book.add_order(Side::Buy, Price{100}, Qty{15});

    EXPECT_EQ(res_agg.fills.size(), 2u);
    EXPECT_EQ(res_agg.fills[0].qty.v, 10u);
    EXPECT_EQ(res_agg.fills[0].resting.v, ask1.id.v);
    EXPECT_EQ(res_agg.fills[1].qty.v, 5u);
    EXPECT_EQ(res_agg.fills[1].resting.v, ask2.id.v);

    auto top = book.top_of_book();
    EXPECT_FALSE(top.has_ask);
}
