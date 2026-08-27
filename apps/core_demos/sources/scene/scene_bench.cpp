// Scene-graph benchmark: recursive resolve (the old approach) vs the linearized
// flat sweep, on a 1M-node hierarchy. Measures the PER-FRAME update (rebuild is a
// structure-change cost, amortized — not timed here).

#include "transform.hpp"
#include "transform_store.hpp"
#include "transform_system.hpp"
#include "world.hpp"

#include <glm/glm.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace ecs;
using namespace scene;
using clk = std::chrono::steady_clock;

// Recursive baseline (the previous design): resolve up the chain, memoized.
static const glm::mat4& resolve(World& w, Entity e, std::vector<std::uint8_t>& done) {
  Transform& t = w.get<Transform>(e);
  const std::uint32_t idx = entity_index(e);
  if (done[idx]) return t.world;
  const glm::mat4 local = t.local();
  if (t.parent != kInvalidEntity && w.valid(t.parent) && w.has<Transform>(t.parent))
    t.world = resolve(w, t.parent, done) * local;
  else
    t.world = local;
  done[idx] = 1;
  return t.world;
}
static void update_recursive(World& w, std::vector<std::uint8_t>& done) {
  std::fill(done.begin(), done.end(), std::uint8_t{0});  // reused buffer (matches old design)
  w.view<Transform>([&](Entity e, Transform&) { resolve(w, e, done); });
}

int main() {
  const std::size_t N = 1'000'000;
  World w;
  std::vector<Entity> ents;
  ents.reserve(N);
  for (std::size_t i = 0; i < N; ++i) {
    Entity e = w.create();
    Transform t;
    t.position = {static_cast<float>(i % 97) * 0.01f, 1.0f, 0.0f};
    if (i > 0) t.parent = ents[(i - 1) / 2];  // balanced binary tree (depth ~20)
    w.add<Transform>(e, t);
    ents.push_back(e);
  }
  std::printf("hierarchy: %zu nodes (binary tree, depth ~20)\n", N);

  std::vector<std::uint8_t> done(N, 0);
  double bestR = 1e30;
  for (int r = 0; r < 6; ++r) {
    const auto t0 = clk::now();
    update_recursive(w, done);
    const auto t1 = clk::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (ms < bestR) bestR = ms;
  }
  const glm::mat4 sampleR = w.get<Transform>(ents[N - 1]).world;

  TransformSystem ts;
  ts.rebuild(w);  // structure-change cost (amortized; not timed)
  double bestL = 1e30;
  for (int r = 0; r < 6; ++r) {
    const auto t0 = clk::now();
    ts.update(w);
    const auto t1 = clk::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (ms < bestL) bestL = ms;
  }
  const glm::mat4 sampleL = w.get<Transform>(ents[N - 1]).world;

  // --- SoA store (system-owned flat arrays) + SIMD ---
  TransformStore store;
  for (std::size_t i = 0; i < N; ++i) {
    TransformStore::Id id = store.create(i > 0 ? static_cast<TransformStore::Id>((i - 1) / 2) : TransformStore::npos);
    store.set_position(id, glm::vec3(static_cast<float>(i % 97) * 0.01f, 1.0f, 0.0f));
  }
  double bestS = 1e30;
  for (int r = 0; r < 6; ++r) {
    const auto t0 = clk::now();
    store.update();
    const auto t1 = clk::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (ms < bestS) bestS = ms;
  }
  const glm::mat4 sampleS = store.world(static_cast<TransformStore::Id>(N - 1));

  const bool matchL = sampleR[3][0] == sampleL[3][0] && sampleR[3][1] == sampleL[3][1] && sampleR[3][2] == sampleL[3][2];
  const bool matchS = sampleR[3][0] == sampleS[3][0] && sampleR[3][1] == sampleS[3][1] && sampleR[3][2] == sampleS[3][2];
  std::printf("recursive  update     : %.2f ms\n", bestR);
  std::printf("linearized update     : %.2f ms  (%.2fx)  [%s]\n", bestL, bestR / bestL, matchL ? "match" : "MISMATCH");
  std::printf("SoA store + SIMD update: %.2f ms  (%.2fx vs recursive, %.2fx vs linearized)  [%s]\n", bestS, bestR / bestS, bestL / bestS,
              matchS ? "match" : "MISMATCH");
  return 0;
}
