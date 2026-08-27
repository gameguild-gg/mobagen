// Micro-benchmark for the ECS hot path: linear iteration over a component pool.
// This is what every system rides on, so its throughput sets the engine's ceiling.
//   build/native/bin/Release/ecs_bench.exe

#include "world.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>

using namespace ecs;
using clk = std::chrono::steady_clock;

struct Comp {
  std::int64_t v;
};

int main() {
  const std::size_t N = 16'000'000;
  World w;
  for (std::size_t i = 0; i < N; ++i) {
    Entity e = w.create();
    w.add<Comp>(e, Comp{static_cast<std::int64_t>(i)});
  }
  std::printf("populated %zu entities\n", N);

  double best = 1e30;
  std::int64_t sink = 0;
  for (int r = 0; r < 6; ++r) {
    std::int64_t acc = 0;
    const auto t0 = clk::now();
    w.view<Comp>([&](Entity, Comp& c) { acc += c.v; });
    const auto t1 = clk::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (ms < best) best = ms;
    sink ^= acc;  // defeat dead-code elimination
  }
  std::printf("view<Comp> iterate %zu: best %.2f ms  (%.0f M elems/s)  [sink=%lld]\n", N, best, static_cast<double>(N) / (best / 1000.0) / 1e6,
              static_cast<long long>(sink));
  return 0;
}
