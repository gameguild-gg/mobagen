// systems-as-jobs: the ECS data layer fed through the work-stealing scheduler.
// A "movement system" (Position += Velocity, with synthetic per-entity work) is
// run two ways over identical worlds — serially via World::view, and in parallel
// via Scheduler::parallel_for over World::apply_range — then results are compared.
//
// Note the modules stay DECOUPLED: ecs knows nothing about jobs. This file (the
// "app") is what glues them — exactly how the renderer/editor will.

#include "scheduler.hpp"  // core/sources/jobs
#include "world.hpp"      // core/sources/ecs

#include <chrono>
#include <cstdio>

using namespace ecs;

struct Position {
  float x, y, z;
};
struct Velocity {
  float x, y, z;
};

// Deterministic, independent per-entity work (no cross-entity deps => parallel-safe).
static inline void integrate(Position& p, const Velocity& v) {
  float x = p.x, y = p.y, z = p.z, dt = 0.016f;
  for (int k = 0; k < 32; ++k) {  // synthetic CPU work so the system is compute-bound
    x += v.x * dt;
    y += v.y * dt;
    z += v.z * dt;
    dt *= 0.999f;
  }
  p.x = x;
  p.y = y;
  p.z = z;
}

static void populate(World& w, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    Entity e = w.create();
    w.add<Position>(e, Position{static_cast<float>(i), 0.f, 0.f});
    w.add<Velocity>(e, Velocity{1.f, static_cast<float>(i % 7), -0.5f});
  }
}

static double checksum(World& w) {
  double s = 0;
  w.view<Position>([&](Entity, Position& p) { s += p.x + p.y + p.z; });
  return s;
}

int main() {
  const std::size_t N = 2'000'000;
  const std::size_t grain = 16'384;

  World ws;
  populate(ws, N);  // serial world
  World wp;
  populate(wp, N);  // parallel world (identical init)

  auto a = std::chrono::steady_clock::now();
  ws.view<Position, Velocity>([](Entity, Position& p, Velocity& v) { integrate(p, v); });
  auto b = std::chrono::steady_clock::now();
  const double serial_ms = std::chrono::duration<double, std::milli>(b - a).count();

  jobs::Scheduler sched;
  const std::size_t n = wp.count<Position>();
  auto c = std::chrono::steady_clock::now();
  jobs::WaitGroup wg;
  sched.parallel_for(
      n, grain,
      [&](std::size_t lo, std::size_t hi) { wp.apply_range<Position, Velocity>(lo, hi, [](Entity, Position& p, Velocity& v) { integrate(p, v); }); },
      wg);
  sched.wait(wg);
  auto d = std::chrono::steady_clock::now();
  const double par_ms = std::chrono::duration<double, std::milli>(d - c).count();

  const double cs = checksum(ws), cp = checksum(wp);
  std::printf("== systems-as-jobs: %zu entities, %u workers ==\n", N, sched.worker_count());
  std::printf("serial  World::view        : %7.1f ms\n", serial_ms);
  std::printf("parallel parallel_for      : %7.1f ms   (%.2fx)\n", par_ms, serial_ms / par_ms);
  std::printf("result check serial=%.0f parallel=%.0f  [%s]\n", cs, cp, cs == cp ? "OK" : "MISMATCH");
  sched.shutdown();
  return 0;
}
