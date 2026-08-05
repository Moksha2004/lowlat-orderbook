# lowlat-orderbook — Design & Build Documentation

Single-symbol limit order book matching engine in C++20, built for
sub-microsecond `add_order` latency on commodity hardware.

This document is the complete record of the build through Day 6: every
component, the reasoning behind each decision, the interview questions each
piece answers, and the mistakes hit along the way. It doubles as interview
prep — the "why" behind each choice is what gets asked.

**Status at Day 6:** the core engine is complete — type system, price levels,
order book, price-time-priority matching, and a zero-allocation memory arena.
Remaining work is real-data plumbing (ITCH replay), concurrency (MPSC feed),
and performance tuning (array price ladder, benchmarks).

---

## 0. Mental model — what an order book actually is

An exchange order book holds every resting buy and sell order for one
instrument. Two questions define it:

- **Price priority:** the best price trades first. Best bid = highest buy;
  best ask = lowest sell.
- **Time priority:** at the *same* price, whoever arrived first trades first
  (FIFO).

An incoming order that crosses the spread (a buy priced at or above the best
ask) *matches* against resting orders and produces trades; whatever quantity
is left over *rests* in the book. That is the entire behavior we implement.

The code is layered so each layer has one job:

```
OrderBook            — the two sides, the index, matching, top-of-book
  └── PriceLevel      — FIFO queue of orders at ONE price (time priority)
        └── Order      — one resting order; also a node in the level's list
  └── OrderArena      — pre-allocated pool the Orders live in (no malloc on hot path)
  └── types           — strong typedefs (Price/Qty/OrderID/ParticipantID), Execution, TopOfBook
```

---

## 1. The type system — `types.hpp`

### 1.1 Strong typedefs

`Price`, `Qty`, `OrderID`, `ParticipantID` are each a one-field struct, not a
`using` alias:

```cpp
struct Price {
    int64_t v;
    constexpr Price() noexcept : v(0) {}
    constexpr explicit Price(int64_t x) : v(x) {}
    auto operator<=>(const Price&) const = default;
};
```

**Why not `using Price = int64_t;`?** Because then `add_order(qty, price)` with
the arguments swapped compiles silently and trades wrong. With a distinct type
and an **`explicit`** constructor, the swap is a *compile error*. The `explicit`
is the load-bearing keyword — it blocks implicit conversion, so an `int` can
never silently become a `Price` and a `Qty` can never be passed where a `Price`
is expected.

`operator<=>() = default` gives all six comparisons (`<`, `>`, `==`, …) for
free, so `Price` sorts correctly as a map key with no extra code.

**The default constructor (`: v(0)`)** was added on Day 6. The arena stores
Orders in a `std::vector<Order>`, and `vector::resize()` must default-construct
its elements — which requires every member (including these typedefs) to be
default-constructible. Zero-initialising is safe and deterministic. Crucially,
adding a default ctor does **not** weaken the type: `explicit` still blocks the
swap bug and implicit conversions. All we gained is "a `Price` with no argument
is 0."

### 1.2 The `Order` struct and its memory layout

```cpp
struct Order {
    Order* next{nullptr};          // 8   intrusive list links (see PriceLevel)
    Order* prev{nullptr};          // 8
    Price price;                   // 8
    OrderID id;                    // 8
    Qty qty;                       // 4
    ParticipantID participant_id;  // 4
    Side side;                     // 1
    // + 7 bytes padding  → 48 bytes total
};
enum class Side : uint8_t { Buy, Sell };
```

`Side` is `enum class` (scoped — no implicit int conversion) and one byte, so it
tucks into padding. The whole struct is **48 bytes — under one 64-byte cache
line.** When the matching loop touches any field of an order, the CPU pulls the
whole order into cache in a single line fetch; there is no second cache miss for
a second field.

### 1.3 Compile-time guards (static_asserts)

```cpp
static_assert(sizeof(Order) == 48, "Order grew — check padding");
static_assert(sizeof(Order) <= 64, "Order must fit one cache line");
static_assert(std::is_trivially_destructible_v<Order>);
```

- The `== 48` pin means any accidental growth (someone adds a field) fails the
  build with a message, instead of silently costing a cache line.
- The `<= 64` states the cache-line contract as code.
- **Trivially destructible** is the important one: the arena recycles Order
  slots *without running destructors*. That is only legal while `Order` owns no
  resources. The day someone adds a `std::string` field, this assert turns a
  silent leak into a loud compile error. These live in the header, not a test,
  so they fire in every file that includes `types.hpp`.

### 1.4 `Execution` and `TopOfBook`

```cpp
struct Execution {        // one fill
    OrderID aggressor;    // the incoming order
    OrderID resting;      // the order it matched against
    Price   price;        // trade prints at the RESTING price
    Qty     qty;
};
struct TopOfBook {        // best bid / best ask snapshot
    bool has_bid; Price best_bid; uint64_t bid_qty;
    bool has_ask; Price best_ask; uint64_t ask_qty;
};
```

### 1.5 Hashing a strong typedef

`std::unordered_map<OrderID, Order*>` needs `std::hash<OrderID>`. The standard
library doesn't know our wrapper, so we specialise it by delegating to the
built-in hasher:

```cpp
template <> struct std::hash<OrderID> {
    size_t operator()(const OrderID& k) const noexcept {
        return std::hash<uint64_t>{}(k.v);
    }
};
```

---

## 2. The price level — `price_level.hpp`

A `PriceLevel` is the FIFO queue of all orders resting at **one** price. It is
an **intrusive doubly-linked list**: the `Order` *is* the node (via its own
`prev`/`next`), so the level allocates nothing — it only rewires pointers to
Orders that the arena owns.

```cpp
class PriceLevel {
    Order*   head_{nullptr};   // oldest — matches first
    Order*   tail_{nullptr};   // newest — matches last
    uint32_t count_{0};
    uint64_t total_qty_{0};    // cached sum of resting qty
public:
    void push_back(Order* o);              // O(1) append at tail
    void remove(Order* o);                 // O(1) unlink
    void reduce_total_qty(uint64_t fill);  // partial-fill bookkeeping
    Order* front() const;  bool empty() const;
    uint32_t count() const;  uint64_t total_qty() const;
};
```

### 2.1 Why intrusive (the key interview answer)

- **vs `std::list`:** `std::list` heap-allocates a node per element and scatters
  them across memory — cache-hostile pointer chasing on the hot path.
- **vs `std::vector`:** a vector gives contiguity but is fatal for cancels for
  *two* reasons. Mid-queue erase is O(n) (shifting), but the deeper problem is
  **pointer invalidation**: our O(1) cancel needs a `unordered_map<OrderID,
  Order*>`, and a vector invalidates every pointer on reallocation and on erase,
  which makes that map impossible. Intrusive nodes give **stable addresses** for
  an order's whole lifetime — a hard requirement.

### 2.2 `push_back` — new orders enter at the tail

Appending at the tail *is* the time-priority rule: no method lets an order enter
anywhere else, so FIFO is structurally guaranteed. `push_back` handles empty
(order becomes both head and tail) and non-empty (link after tail) and updates
`count_` and `total_qty_`.

### 2.3 `remove` — two questions, not four cases

Instead of memorising four unlink cases (only / head / tail / middle):

- *Does `o` have a prev?* Yes → `o->prev->next = o->next`. No → `o` was head, so
  `head_ = o->next`.
- *Does `o` have a next?* Yes → `o->next->prev = o->prev`. No → `o` was tail, so
  `tail_ = o->prev`.

All four cases fall out of those two independent questions. Then decrement
count/qty and **scrub the removed order's pointers** (`o->prev = o->next =
nullptr`) — a stale link on a removed order becomes a use-after-free the moment
the arena recycles that slot.

### 2.4 Why cache `count_` and `total_qty_`

Strategies query market depth ("how much size at the best bid?") constantly. If
the level didn't keep a running sum, answering would walk every order — O(n) on
the hottest read. Instead `push_back`/`remove` each pay one add/subtract, and
the query is a single field read. Pennies at write time to make reads free.

### 2.5 `reduce_total_qty` — the partial-fill subtlety (solved cleanly)

When matching partially fills a resting order (10 → 6), the order stays in the
level but the level's cached total must drop by the filled amount. Method:

```cpp
void reduce_total_qty(uint64_t fill) {
    assert(total_qty_ >= fill);   // unsigned underflow guard
    total_qty_ -= fill;
}
```

The elegance is how this interacts with `remove`: `remove` subtracts the order's
*current* `qty.v`. So on a **full** fill we zero the order's qty first, then
`remove` subtracts 0 — no double-count. On a **partial** fill the order stays
and `reduce_total_qty` accounts for the fill. Either path keeps `total_qty_`
exact. (The order of operations matters: subtract from `o->qty.v` *before*
calling `remove`.)

---

## 3. The order book — `order_book.hpp`

```cpp
class OrderBook {
    std::map<Price, PriceLevel, std::greater<Price>> bids_;  // highest first
    std::map<Price, PriceLevel>                      asks_;  // lowest first
    std::unordered_map<OrderID, Order*>              index_; // O(1) cancel
    OrderArena                                       arena_{1'000'000};
    uint64_t                                         next_id_{1};
};
```

### 3.1 The two sides and their opposite orderings

Both sides are sorted maps, but the comparators point opposite ways because
"best" means opposite directions:

- **Bids** use `std::greater<Price>` → highest price is `begin()` (best bid).
- **Asks** use the default `std::less` → lowest price is `begin()` (best ask).

So `begin()` is always the best price on either side — matching and
top-of-book both just look at `begin()`.

### 3.2 The index — O(1) cancel

`index_` maps `OrderID → Order*`. Cancel looks up the pointer directly (no
searching), and because the `Order` carries its own `side` and `price`, we route
to the exact level in O(1), unlink, and — if the level is now empty — **erase
the price key** so empty levels don't accumulate.

### 3.3 `top_of_book`

Reads `begin()` on each side (guarding the empty-book case, where `begin() ==
end()`), returning best price and the level's cached `total_qty()`.

---

## 4. The matching engine

The heart of the project. Before an incoming order rests, it must try to trade.

### 4.1 Crossing rules

- A **buy** crosses an ask when `buy_price >= ask_price`; it walks asks
  low→high.
- A **sell** crosses a bid when `sell_price <= bid_price`; it walks bids
  high→low.

Both are already the natural `begin()`-first order of the respective maps.

### 4.2 The algorithm (one fill at a time)

While the incoming order has quantity left AND the opposing book is non-empty
AND its best level crosses:

1. Take `front()` of the best opposing level — the oldest order (time
   priority, guaranteed by PriceLevel's FIFO).
2. `fill = min(incoming.qty, resting.qty)`.
3. Record an `Execution` **at the resting order's price**.
4. Subtract `fill` from both orders; `reduce_total_qty(fill)` on the level.
5. If the resting order is fully filled: `remove` it, erase from `index_`,
   `arena_.free` it. If the level emptied, erase the price key.
6. Stop when the incoming order is exhausted.

Back in `add_order`: run matching first; only the *residual* rests.

### 4.3 Three things that are the interview questions

- **Trade price is the resting price, not the aggressor's.** A buy at 102
  hitting a resting ask at 100 trades at **100** — the resting order set the
  terms; the aggressor accepts them. (Price improvement goes to the aggressor.)
- **Partial fills both directions:** incoming larger than one resting order
  (keep walking), and incoming smaller (resting order stays, reduced).
- **`total_qty_` stays exact** through the `reduce_total_qty` / `remove`
  interplay described in §2.5.

### 4.4 One code path for both sides — `match_side`

The buy and sell logic is identical except for which map and which cross
predicate. Rather than duplicate ~25 lines (where the two copies inevitably
drift apart), it's a single templated helper taking the opposing map and a
`crosses` lambda:

```cpp
fills = (side == Side::Buy)
  ? match_side(incoming, asks_, [](Price p, Price ask){ return p >= ask; })
  : match_side(incoming, bids_, [](Price p, Price bid){ return p <= bid; });
```

### 4.5 Self-trade prevention: deliberately OFF for now

If an order would match its own participant, it currently trades. STP
(CancelNewest per the SPEC) is Day 13 — bolting it into the core loop now would
tangle the logic before the fundamentals are proven.

### 4.6 `add_order` return value

```cpp
enum class Status { Filled, Rested, RestRejected };
struct AddResult { OrderID id; std::vector<Execution> fills; Status status; };
```

`Filled` = fully matched, nothing rested. `Rested` = residual is now in the
book. `RestRejected` = the arena was full so the residual couldn't rest (any
fills that happened still executed).

---

## 5. The memory arena — `arena_allocator.hpp`

The piece that makes this a *low-latency* project rather than a data-structures
exercise. SPEC invariant #2: **zero heap allocation on the hot path.**

### 5.1 Why malloc is banned on the hot path

`malloc` can take hundreds of nanoseconds and is *unpredictable* — it may take a
lock or enter the kernel. With a p99 target of 800 ns, a single hot-path malloc
blows the budget. So we allocate all memory **once**, up front, and hand out
slots with no syscalls afterward.

### 5.2 Structure — pool + intrusive free list

```cpp
class OrderArena {
    std::vector<Order> pool_;      // ONE allocation, in the constructor
    Order*             free_head_; // stack of free slots
    size_t capacity_, available_;
};
```

The free list costs **zero extra memory**: a free slot isn't holding a live
order, so its `Order::next` is unused — we thread the free list through it. The
constructor links every slot to the next; `allocate()` pops the head, `free()`
pushes onto it. Both are O(1) and allocation-free.

**The lifecycle invariant:** a slot is *either* free (linked via `next` in the
arena's list) *or* live (linked via `next` inside a PriceLevel) — never both. So
`next` has exactly one owner at any instant, as long as we always
allocate → fill → link-into-level, and unlink-from-level → free, in that order.

### 5.3 Exhaustion: strict reject, not grow

`allocate()` returns `nullptr` when full, and `add_order` rejects the residual
(`RestRejected`). We do **not** grow the pool: `vector::resize()` would relocate
the backing store and invalidate every live `Order*` in the levels and the
index — a catastrophic latency spike and correctness break. A strict reject
protects deterministic latency.

### 5.4 Capacity: 1,000,000 slots

At 64 bytes/Order that's ~64 MB of contiguous memory — large enough to absorb
market-open bursts, small enough to be friendly to L3 cache and the TLB.

### 5.5 Wiring

`OrderBook` holds `OrderArena arena_{1'000'000}` instead of the old
`std::deque`. Resting an order calls `arena_.allocate()`; cancel and full-fill
call `arena_.free()`. This also retired the Day-4 limitation where storage grew
forever — slots are now recycled.

---

## 6. The zero-allocation proof (the benchmark that matters)

An `LD_PRELOAD` shim (`bench/malloc_shim.c`) intercepts `malloc` and counts
calls while tracking is on (operator `new` routes through `malloc`, so C++
allocations are caught). `bench/bench_arena.cpp` runs 100,000 add+cancel
iterations and reports the count.

**Measured result:**

| Configuration | mallocs / 100k iterations | per iteration |
|---|---|---|
| Arena (Orders pooled) | 200,000 | 2 |
| `new`/`delete` baseline | 300,000 | 3 |
| **Delta** | **100,000** | **1** |

**Interpretation — and the honest claim.** The delta is exactly one allocation
per order: the arena eliminates **100% of per-order heap allocation (100,000 →
0).** The residual 200k are `std::map` and `std::unordered_map` *node*
allocations — the containers, not the Orders. The arena cannot touch those.

The correct README claim is therefore *not* "zero mallocs proven" (a sharp
interviewer would immediately ask about the map nodes). It is: **"the arena
eliminates all per-order allocation; the residual container-node allocations are
exactly what the array price ladder (Day 9) removes."** Measuring it, knowing
what each layer costs, and knowing what's next is the senior answer.

---

## 7. Mistakes hit during the build (kept as a checklist)

These recurred; they are all things the compiler catches in seconds — the fix is
**build before declaring done**, every time.

1. **Designated-initializer order.** C++20 requires initializers in *declaration*
   order. `Order{.id=…, .price=…}` fails because `id` is declared after `next`
   and `price`.
2. **Narrowing in braced init.** `Qty{some_uint64}` is ill-formed — `Qty` wraps
   `uint32_t`. Match widths (`fill` is `uint32_t`).
3. **Header-only target + stray `.cpp`.** The `orderbook` CMake target is
   `INTERFACE` (header-only). Method bodies go *in the headers* (also correct:
   in-class = implicitly inline, so the hot path inlines without LTO).
4. **Testing against a mock type.** The arena's first test used a fake `Order`
   with plain fields; it passed but hid that the *real* `Order` wasn't
   default-constructible, so `vector::resize()` wouldn't compile. Test against
   the real type.

---

## 8. File-by-file reference

| File | Contents |
|---|---|
| `include/orderbook/types.hpp` | Strong typedefs, `Order`, `Side`, `Execution`, `TopOfBook`, `std::hash<OrderID>`, layout static_asserts |
| `include/orderbook/price_level.hpp` | `PriceLevel` — intrusive FIFO queue at one price |
| `include/orderbook/order_book.hpp` | `OrderBook` — sides, index, matching (`match_side`), `add_order`/`cancel_order`/`top_of_book`, arena wiring |
| `include/orderbook/arena_allocator.hpp` | `OrderArena` — fixed pool + intrusive free list |
| `tests/test_types.cpp` | typedef comparisons, field access |
| `tests/test_price_level.cpp` | all four removal cases, FIFO preservation, pointer scrub |
| `tests/test_order_book.cpp` | top-of-book, cancel behavior, 6 matching scenarios |
| `tests/test_arena.cpp` | allocate/exhaust, free/recycle, zero-capacity |
| `bench/malloc_shim.c`, `bench/bench_arena.cpp`, `bench/run_alloc_proof.sh` | zero-allocation proof |

---

## 9. Commit history (Days 1–6)

```
init: scaffold, SPEC.md, CMake
feat: core types + test scaffold
feat: intrusive PriceLevel with tests
feat: map-backed OrderBook (no matching)
feat: price-time priority matching engine
feat: fixed-capacity arena with intrusive free list
feat: wire arena into OrderBook + zero-alloc proof
```

## 10. What's next

- **Day 7:** FIX-lite text parser (`std::from_chars`) → drive the book from
  messages instead of direct calls.
- **Week 2:** ITCH binary replay (mmap real NASDAQ data), array-indexed price
  ladder (removes the `std::map` node allocations from §6), baseline latency
  benchmark suite.
- **Week 3:** MPSC feed→engine pipeline, self-trade prevention, perf tuning
  (branch hints, false-sharing audit), LMAX Disruptor comparison.
