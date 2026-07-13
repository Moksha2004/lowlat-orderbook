#pragma once
#include <cstdint>
// Day 2 task — YOU implement. Contract from SPEC.md §2.
// TODO: Side enum (Buy/Sell); strong typedefs Price/Qty/OrderID/ParticipantID
//       (struct-wrapped, not raw using-aliases — you want type errors, not silent mixups);
//       Order struct with intrusive prev/next pointers (see Playbook: NO std::list).
// Think: field order for cache friendliness — hot fields (price, qty, next) in the first 64 bytes.
