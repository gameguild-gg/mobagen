#!/usr/bin/env bash
# Run every core demo on macOS/Linux. Build first:
#   cmake -B build -DBUILD_CORE=ON -DUSE_GDCM=ON -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
# (fiber_demo is Windows-only and intentionally not listed; coro_demo is the
#  portable equivalent. Single-config generators put exes in build/bin/ with no .exe.)
set -u
cd "$(dirname "$0")/.."
BIN=build/bin

for d in ecs_demo world_demo reactive_demo messaging_demo scene_demo \
         jobs_demo jobs_c_demo systems_demo engine_demo net_demo \
         volume_io_test coro_demo jobs_bench; do
    echo
    echo "===================== $d ====================="
    if [ -x "$BIN/$d" ]; then
        "$BIN/$d"
    else
        echo "[missing] $BIN/$d  -- build it first (see header)"
    fi
done
echo
echo "===================== all demos finished ====================="
