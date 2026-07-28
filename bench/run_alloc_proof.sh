#!/usr/bin/env bash
# Zero-allocation proof for the arena. Run from repo root:
#   bash bench/run_alloc_proof.sh
set -euo pipefail
cd "$(dirname "$0")/.."

gcc -shared -fPIC -O2 bench/malloc_shim.c -o bench/libshim.so -ldl
g++ -std=c++20 -O2 -Iinclude bench/bench_arena.cpp -o bench/bench_arena \
    -Lbench -l:libshim.so -Wl,-rpath,bench

LD_PRELOAD=bench/libshim.so ./bench/bench_arena
