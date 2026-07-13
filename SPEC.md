# SPEC — Low-Latency Order Book Matching Engine
**Author:** Mokshagna Prattipati · **Day 1:** 2026-07-13 · **Ship:** v1.0.0 by 2026-08-08

> This is the design contract. Scope changes land here IN THE SAME COMMIT as the code.
> Sections marked ➤ YOU WRITE are yours — a spec you didn't write is a spec you can't defend in an interview.

## 1. Problem statement
Single-symbol limit order book with price-time priority, engineered for
sub-microsecond add_order latency on commodity hardware.

## 2. Public API (frozen after Day 3)
```cpp
OrderID  add_order(Side, Price, Qty, ParticipantID);
bool     cancel_order(OrderID);
bool     modify_order(OrderID, Price new_price, Qty new_qty);  // ➤ YOU WRITE: does modify lose time priority? Decide + justify.
TopOfBook top_of_book() const;
```

## 3. Invariants (violating any of these is a bug, not a tradeoff)
- Price-time priority: better price first; FIFO within a price level.
- Zero heap allocation on the hot path (proven by malloc-count test, Day 6).
- Single symbol, single matching thread. Feed ingestion may be multi-threaded (Week 3 MPSC).
- ➤ YOU WRITE: overflow behavior of the price ladder; qty=0 semantics; self-match default mode.

## 4. Performance targets (from Playbook §4)
| Op | p50 | p99 | p999 |
|---|---|---|---|
| add_order | < 200 ns | < 800 ns | < 2 µs |
Throughput: > 10M ops/sec single-threaded. Methodology: pinned core, performance governor, warm cache, rdtsc.

## 5. Design decisions log  ➤ YOU WRITE (append-only, dated)
- 2026-07-13: intrusive doubly-linked list per price level because ____ (vs std::list: ____, vs vector: ____).

## 6. Future work (scope parking lot — nothing here gets built before v1.0.0)
- (empty)
