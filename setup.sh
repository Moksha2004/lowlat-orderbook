#!/usr/bin/env bash
# Day-1 toolchain setup — Ubuntu 22.04+ / WSL2. Run: bash setup.sh
set -euo pipefail
sudo apt update
sudo apt install -y build-essential cmake git clang-17 clang-tools-17 linux-tools-common linux-tools-generic
git --version && cmake --version && clang++-17 --version
# HdrHistogram_c (system-wide)
if [ ! -d /tmp/HdrHistogram_c ]; then
  git clone https://github.com/HdrHistogram/HdrHistogram_c.git /tmp/HdrHistogram_c
fi
cmake -S /tmp/HdrHistogram_c -B /tmp/HdrHistogram_c/build && cmake --build /tmp/HdrHistogram_c/build -j"$(nproc)" && sudo cmake --install /tmp/HdrHistogram_c/build
echo "== toolchain OK. Now: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build =="
# NOTE (WSL2): 'perf' may need: sudo apt install linux-tools-$(uname -r) or a manual build from WSL2 kernel source.
