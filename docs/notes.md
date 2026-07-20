# Project Notes — lowlat-orderbook
Running log of what was built, why, and the arguments behind each decision.
(These are interview prep as much as documentation — every "why" here is a
question someone at Optiver/Tower/AlphaGrep will actually ask.)

---

## Day 1 (Jul 13) — Scaffold + SPEC

**What exists:** repo layout, CMake (C++20, `-O3 -march=native`, gtest +
google-benchmark via FetchContent), SPEC.md, empty compiling test.

**The SPEC decisions and their one-line defenses:**

- **Modify semantics** — price change or qty increase loses time priority
  (= cancel + re-add); qty decrease keeps its spot.
  *Why: otherwise traders hold queue position with 1-share orders and inflate
  them when the market moves — priority you didn't earn. Qty decrease harms
  nobody behind you. Matches CME/NASDAQ behavior.*
- **Price ladder** — flat array for mid ± 1000 ticks, `std::map` fallback outside.
  *Why: volume clusters at the spread → O(1) array hit on the hot path. The map
  is for CORRECTNESS not speed: a fixed array can't cover an unbounded price
  range, and we refuse to resize/reallocate on the hot path.*
- **qty = 0 rejected at the API** — garbage input, not an order.
- **STP default CancelNewest** — the resting order earned its priority; the
  incoming order is the one making the mistake.

## Day 2 (Jul 15 commit) — types.hpp

**What exists:** strong typedefs (Price/Qty/OrderID/ParticipantID), `Side`
enum, `Order` struct, compile-time guards.

- **Strong typedefs over raw `using` aliases:** a one-field struct with a
  `constexpr explicit` ctor and `operator<=>`. The `explicit` is load-bearing —
  swapping (price, qty) argument order becomes a COMPILE ERROR instead of a
  silent trading bug.
- **Order layout:** intrusive `prev`/`next` pointers (8+8), price (8), id (8),
  qty (4), participant_id (4), side (1) → 41 bytes payload → **48 bytes with
  padding**. Whole order fits one cache line; touching any field pulls in all
  of them.
- **Three header static_asserts:**
  - `sizeof(Order) == 48` — any accidental growth fails the build with a paper trail.
  - `sizeof(Order) <= 64` — the cache-line contract itself.
  - `is_trivially_destructible_v<Order>` — the arena will recycle Order slots
    WITHOUT running destructors. Legal only while Order owns no resources.
    If someone adds a `std::string` field, this turns a silent leak into a
    loud compile error.
- Asserts live in the header, not tests: compile-time, enforced in every
  translation unit, fires the moment anyone edits Order — not when the test
  target happens to rebuild.

## Day 3 (Jul 20) — PriceLevel (intrusive FIFO queue)

**What exists:** `price_level.hpp` — intrusive doubly-linked list per price
level. `push_back` O(1), `remove` O(1), front/empty/count/total_qty. Six tests
covering all four removal cases + FIFO preservation + pointer scrubbing.

- **Why intrusive (the SPEC §5 argument, now real):** the Order IS the node —
  no separate node allocation, and an order's address is stable for its
  lifetime. `std::list` allocates per node and scatters them across the heap
  (cache-hostile). `std::vector` is fatal for a different reason: O(1) cancel
  needs a hash map OrderID → Order*, and vectors invalidate pointers on every
  reallocation/erase. Stable addresses are a hard requirement.
- **remove() as two independent questions** (not four memorized cases):
  does o have a prev? (fix prev's next, else move head). Does o have a next?
  (fix next's prev, else move tail). All four cases fall out.
- **Pointer scrubbing after remove:** o->prev = o->next = nullptr. A removed
  order still pointing into the list = use-after-free when the arena recycles
  the slot.
- **Implementations in-class, not in a .cpp:** in-class definitions are
  implicitly inline → the compiler can inline push_back/remove into the
  matching loop without LTO. (Also: the CMake `orderbook` target is a
  header-only INTERFACE library — a stray .cpp has nothing to compile into.)
- **Test discipline learned:** walk the list BOTH directions after a middle
  removal — half-broken lists pass count checks and forward walks. And the
  Remove_Tail test pushes a fresh order afterward to catch a stale tail_.

**C++ gotchas hit on Day 3 (remember these):**
1. C++20 designated initializers must follow declaration order.
2. `Qty{some_uint64}` is a narrowing error in braced init — match widths.

## Reading done
- Carl Cook, "When a Microsecond Is an Eternity" (CppCon 2017) — in depth.
- Next: LMAX Disruptor whitepaper, Mechanical Sympathy posts (false sharing,
  single-writer principle), ITCH 5.0 spec pp. 1–30.

## Next up (Day 4)
Map-backed OrderBook: `std::map<Price, PriceLevel>` bids descending / asks
ascending, add_order / cancel_order / top_of_book, NO matching yet.
