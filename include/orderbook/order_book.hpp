#pragma once
#include "orderbook/types.hpp"
#include "orderbook/price_level.hpp"
#include "orderbook/arena_allocator.hpp"
#include <map>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <functional>

// Single-symbol limit order book with price-time priority matching.
// Implementations in-class (inline): hot path, and the CMake target is
// header-only.
class OrderBook {
    std::map<Price, PriceLevel, std::greater<Price>> bids_;   // highest first
    std::map<Price, PriceLevel>                      asks_;   // lowest first
    std::unordered_map<OrderID, Order*>              index_;
    OrderArena                                       arena_{1'000'000}; // Orders live here
    uint64_t                                         next_id_{1};

    // Walk the opposing book best-price-first, filling until the incoming order
    // is exhausted or can no longer cross. Mutates incoming.qty down.
    // STP is intentionally OFF here — self-trades allowed until Day 13.
    template <class OppMap, class Crosses>
    std::vector<Execution> match_side(Order& incoming, OppMap& opp, Crosses crosses) {
        std::vector<Execution> fills;
        auto it = opp.begin();
        while (incoming.qty.v > 0 && it != opp.end() && crosses(incoming.price, it->first)) {
            PriceLevel& level = it->second;
            while (incoming.qty.v > 0 && !level.empty()) {
                Order* resting = level.front();
                uint32_t fill_qty = std::min(incoming.qty.v, resting->qty.v);

                fills.push_back(Execution{
                    .aggressor = incoming.id,
                    .resting   = resting->id,
                    .price     = resting->price,   // trade at the resting price
                    .qty       = Qty{fill_qty}
                });

                // Zero-out BEFORE remove() so remove() subtracts 0, not the
                // pre-fill qty (avoids double-counting total_qty_).
                incoming.qty.v -= fill_qty;
                resting->qty.v -= fill_qty;
                level.reduce_total_qty(fill_qty);

                if (resting->qty.v == 0) {
                    level.remove(resting);
                    index_.erase(resting->id);
                    arena_.free(resting);   // recycle the filled slot
                }
            }
            it = level.empty() ? opp.erase(it) : std::next(it);
        }
        return fills;
    }

public:
    // RestRejected: the residual could not rest because the arena is full.
    // Any fills in `fills` still executed — only the leftover is dropped.
    enum class Status { Filled, Rested, RestRejected };

    struct AddResult {
        OrderID id;
        std::vector<Execution> fills;
        Status status;
    };

    AddResult add_order(Side side, Price price, Qty qty,
                        ParticipantID participant = ParticipantID{0}) {
        OrderID id{next_id_++};
        Order incoming{
            .next = nullptr,
            .prev = nullptr,
            .price = price,
            .id = id,
            .qty = qty,
            .participant_id = participant,
            .side = side
        };

        std::vector<Execution> fills =
            (side == Side::Buy)
              ? match_side(incoming, asks_, [](Price p, Price ask) { return p >= ask; })
              : match_side(incoming, bids_, [](Price p, Price bid) { return p <= bid; });

        if (incoming.qty.v == 0) {
            return {id, std::move(fills), Status::Filled};
        }

        // Residual rests — take a slot from the arena.
        Order* o = arena_.allocate();
        if (!o) [[unlikely]] {
            return {id, std::move(fills), Status::RestRejected};
        }
        *o = incoming;                 // copy fields into the pooled slot
        if (side == Side::Buy) {
            bids_[price].push_back(o);
        } else {
            asks_[price].push_back(o);
        }
        index_[id] = o;
        return {id, std::move(fills), Status::Rested};
    }

    bool cancel_order(OrderID id) {
        auto it = index_.find(id);
        if (it == index_.end()) {
            return false;
        }
        Order* o = it->second;
        if (o->side == Side::Buy) {
            auto lvl = bids_.find(o->price);
            lvl->second.remove(o);
            if (lvl->second.empty()) bids_.erase(lvl);
        } else {
            auto lvl = asks_.find(o->price);
            lvl->second.remove(o);
            if (lvl->second.empty()) asks_.erase(lvl);
        }
        index_.erase(it);
        arena_.free(o);   // recycle the cancelled slot
        return true;
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
