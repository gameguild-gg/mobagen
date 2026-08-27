// Multi-component iteration benchmark: view<A,B> (random sparse probe into B per
// entity) vs a co-ordered Group (contiguous, no probe). B is added in SHUFFLED
// order so its dense layout diverges from A's — the realistic case where the
// probe actually costs cache misses.

#include "group.hpp"
#include "world.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using namespace ecs;
using clk = std::chrono::steady_clock;

struct A {
  std::int64_t v;
};
struct B {
  std::int64_t v;
};

int main() {
  const std::size_t N = 4'000'000;
  World w;
  std::vector<Entity> ents;
  ents.reserve(N);
  for (std::size_t i = 0; i < N; ++i) {
    Entity e = w.create();
    w.add<A>(e, A{static_cast<std::int64_t>(i)});
    ents.push_back(e);
  }
  // Add B in SHUFFLED entity order -> B's dense layout != A's (orders diverge,
  // as they would after real add/remove churn).
  std::vector<Entity> shuffled = ents;
  std::mt19937_64 rng(12345);
  std::shuffle(shuffled.begin(), shuffled.end(), rng);
  for (Entity e : shuffled) w.add<B>(e, B{static_cast<std::int64_t>(entity_index(e)) * 2});
  std::printf("populated %zu entities with A and (shuffled) B\n", N);

  // Baseline: view<A,B> — random sparse probe into B per element.
  double bestV = 1e30;
  std::int64_t sumV = 0;
  for (int r = 0; r < 6; ++r) {
    std::int64_t acc = 0;
    const auto t0 = clk::now();
    w.view<A, B>([&](Entity, A& a, B& b) { acc += a.v + b.v; });
    const auto t1 = clk::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (ms < bestV) bestV = ms;
    sumV = acc;
  }

  // Group: co-order once, then contiguous iteration (no probe).
  Group<A, B> g(w);
  g.refresh();
  double bestG = 1e30;
  std::int64_t sumG = 0;
  for (int r = 0; r < 6; ++r) {
    std::int64_t acc = 0;
    const auto t0 = clk::now();
    g.each([&](A& a, B& b) { acc += a.v + b.v; });
    const auto t1 = clk::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (ms < bestG) bestG = ms;
    sumG = acc;
  }

  std::printf("view<A,B> (sparse probe): %.2f ms  (sum=%lld)\n", bestV, static_cast<long long>(sumV));
  std::printf("group     (co-ordered)  : %.2f ms  (%.2fx)  [%s]\n", bestG, bestV / bestG, sumV == sumG ? "match" : "MISMATCH");
  return 0;
}
