# SPEC — Low-Latency Order Book Matching Engine

**Author:** Mokshagna Prattipati · **Day 1:** 2026-07-13 · **Ship:** v1.0.0 by 2026-08-08

> This is the design contract. Scope changes land here IN THE SAME COMMIT as the code.

## 1. Problem statement

Single-symbol limit order book with price-time priority, engineered for
sub-microsecond add_order latency on commodity hardware.

## 2. Public API (frozen after Day 3)

```cpp
OrderID   add_order(Side, Price, Qty, ParticipantID);
bool      cancel_order(OrderID);
bool      modify_order(OrderID, Price new_price, Qty new_qty);
TopOfBook top_of_book() const;
```

**Modify semantics & time priority:** a `modify_order` that changes the price or
increases quantity loses time priority (treated as a cancel + new add). A decrease
in quantity keeps its place in line.

*Justification:* if you let people increase their order size without losing
priority, traders will just spam 1-share orders at every price level to hold a
spot. When the market moves, they bump it to 10,000 shares and cut in front of
everyone. Decreasing size doesn't screw over the people waiting behind you, so
you keep your spot. This matches real venue behavior (CME, NASDAQ).

## 3. Invariants (violating any of these is a bug, not a tradeoff)

- Price-time priority: better price first; FIFO within a price level.
- Zero heap allocation on the hot path (proven by malloc-count test, Day 6).
- Single symbol, single matching thread. Feed ingestion may be multi-threaded (Week 3 MPSC).
- **Price ladder overflow:** the main ladder is a flat array for mid-price ± 1000
  ticks; anything outside falls back to a `std::map`.
  *Justification:* nearly all volume happens near the spread (the hot path), where
  array indexing is O(1). The map is not there for speed — it exists because a
  fixed-size array cannot cover an unbounded price range, and we refuse to resize
  or reallocate on the hot path. Far-out orders get correctness at O(log n); the
  hot path never pays for them.
- **Quantity validation:** `qty = 0` is rejected at the API boundary. It's garbage
  input, not an order.
- **Self-trade prevention (STP):** default mode is CancelNewest.
  *Justification:* the resting order was there first — it earned its time
  priority. If my own algo accidentally sends a new order that crosses it, the
  incoming order is the one making the mistake. Kill the new one, protect the
  resting liquidity.

## 4. Performance targets (from Playbook §4)

| Op | p50 | p99 | p999 |
|---|---|---|---|
| add_order | < 200 ns | < 800 ns | < 2 µs |

Throughput: > 10M ops/sec single-threaded.
Methodology: pinned core, performance governor, warm cache, rdtsc.

## 5. Design decisions log (append-only, dated)

- **2026-07-13: intrusive doubly-linked list for price-level queues.**
  The Order struct itself carries the prev/next pointers, so there is no separate
  node allocation at all, and an order's address is stable for its entire
  lifetime. (Orders live in a pre-allocated arena — that's the arena's job; the
  intrusive list's job is zero-allocation linking and O(1) unlink.)
  - *vs `std::list`:* calls `new` for every node; nodes scatter across the heap
    and the CPU cache chokes chasing pointers.
  - *vs `std::vector`:* mid-queue cancels force an O(n) shift of every order
    behind the canceled one — but the fatal problem is pointer invalidation.
    O(1) cancel (Week 2) requires a hash map of OrderID → Order*. A vector
    invalidates pointers on every reallocation and every erase, which makes that
    hash map impossible. Stable addresses are a hard requirement; vector cannot
    provide them.

## 6. Future work (scope parking lot — nothing here gets built before v1.0.0)

- (empty)
