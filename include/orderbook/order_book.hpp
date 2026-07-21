#pragma once
#include "orderbook/types.hpp"
#include "orderbook/price_level.hpp"
#include <map>
#include <unordered_map>
#include <deque>
#include <functional>

// Single-symbol limit order book. No matching yet (Day 5) — this stage just
// stores resting orders and answers top_of_book / cancel.
//
// Implementations are in-class (implicitly inline): add/cancel are hot-path,
// and the `orderbook` CMake target is header-only, so there is no .cpp to
// compile into.
class OrderBook {
    // Bids: highest price first. Asks: lowest price first (map default).
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel>                      asks_;

    std::unordered_map<OrderID, Order*> index_;   // OrderID -> Order*, O(1) cancel
    std::deque<Order>                   storage_; // owns Orders; stable addresses
    uint64_t                            next_id_{1};

public:
    OrderID add_order(Side side, Price price, Qty qty, ParticipantID participant) {
        OrderID id{next_id_++};

        // Construct in stable storage (deque never relocates existing elements,
        // unlike vector — the index_ pointers must stay valid).
        storage_.push_back(Order{
            .next = nullptr,
            .prev = nullptr,
            .price = price,
            .id = id,
            .qty = qty,
            .participant_id = participant,
            .side = side
        });
        Order* o = &storage_.back();

        // operator[] creates the PriceLevel if this price is new.
        if (side == Side::Buy) {
            bids_[price].push_back(o);
        } else {
            asks_[price].push_back(o);
        }
        index_[id] = o;
        return id;
    }

    bool cancel_order(OrderID id) {
        auto it = index_.find(id);
        if (it == index_.end()) {
            return false;
        }
        Order* o = it->second;

        // Order carries its own side + price, so we route in O(1).
        if (o->side == Side::Buy) {
            auto lvl = bids_.find(o->price);
            lvl->second.remove(o);
            if (lvl->second.empty()) {
                bids_.erase(lvl);   // don't leave empty price levels behind
            }
        } else {
            auto lvl = asks_.find(o->price);
            lvl->second.remove(o);
            if (lvl->second.empty()) {
                asks_.erase(lvl);
            }
        }
        index_.erase(it);
        return true;
        // NOTE (limitation): the Order stays in storage_ — deque grows for the
        // book's lifetime. Acceptable now; the Day-5 arena reclaims slots.
    }

    TopOfBook top_of_book() const {
        TopOfBook top;
        if (!bids_.empty()) {
            top.has_bid  = true;
            top.best_bid = bids_.begin()->first;
            top.bid_qty  = bids_.begin()->second.total_qty();
        }
        if (!asks_.empty()) {
            top.has_ask  = true;
            top.best_ask = asks_.begin()->first;
            top.ask_qty  = asks_.begin()->second.total_qty();
        }
        return top;
    }
};
