// Performance benchmark: fork-join recursive reduce — the canonical work-stealing
// workload. A task splits its range, kicks two sub-tasks onto its OWN deque,
// `co_await`s them, and combines. This exercises the Chase-Lev deques + stealing
// + coroutine suspension all at once, and lets us measure SCALING across workers.
//
//   cmake --build build/native --config Release --target jobs_bench
//   build/native/bin/Release/jobs_bench.exe

#include "scheduler.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

using namespace jobs;

static std::atomic<std::uint64_t> g_total{0};

// splitmix64 step: real per-element CPU work the optimizer can't fold away.
static inline std::uint64_t mix(std::uint64_t i) {
  std::uint64_t x = i * 0x9E3779B97F4A7C15ull;
  x ^= x >> 29;
  x *= 0xBF58476D1CE4E5B9ull;
  x ^= x >> 32;
  return x;
}

// Hot loop in a PLAIN function (not the coroutine body) so it optimizes fully.
static std::uint64_t range_sum(std::uint64_t lo, std::uint64_t hi) {
  std::uint64_t acc = 0;
  for (std::uint64_t i = lo; i < hi; ++i) acc += mix(i);
  return acc;
}

static Task sum_task(Scheduler& s, std::uint64_t lo, std::uint64_t hi, std::uint64_t grain) {
  if (hi - lo <= grain) {
    g_total.fetch_add(range_sum(lo, hi), std::memory_order_relaxed);
    co_return;
  }
  const std::uint64_t mid = lo + (hi - lo) / 2;
  WaitGroup wg;
  s.kick(sum_task(s, lo, mid, grain), wg);  // onto this worker's own deque
  s.kick(sum_task(s, mid, hi, grain), wg);
  co_await wg;  // suspend; worker runs/steals subtasks
  co_return;
}

static std::uint64_t serial_ref(std::uint64_t n) {
  std::uint64_t acc = 0;
  for (std::uint64_t i = 0; i < n; ++i) acc += mix(i);
  return acc;
}

int main() {
  const std::uint64_t N = 64ull * 1024 * 1024;
  const std::uint64_t grain = 64 * 1024;
  std::printf("== fork-join work-stealing benchmark ==\n");
  std::printf("N=%llu  grain=%llu  (~%llu leaf tasks)\n\n", (unsigned long long)N, (unsigned long long)grain, (unsigned long long)(N / grain));

  const auto s0 = std::chrono::steady_clock::now();
  const std::uint64_t ref = serial_ref(N);
  const auto s1 = std::chrono::steady_clock::now();
  const double serial_ms = std::chrono::duration<double, std::milli>(s1 - s0).count();
  std::printf("serial baseline (no scheduler): %.1f ms\n\n", serial_ms);

  const unsigned hw = std::thread::hardware_concurrency();
  std::printf("hardware_concurrency = %u\n", hw);
  double t1 = 0.0;
  for (unsigned w : {1u, 2u, 4u, 8u, 16u}) {
    if (w > hw) break;
    Scheduler s(w);
    g_total.store(0, std::memory_order_relaxed);
    const auto a = std::chrono::steady_clock::now();
    WaitGroup root;
    s.kick(sum_task(s, 0, N, grain), root);
    s.wait_idle();
    const auto b = std::chrono::steady_clock::now();
    s.shutdown();
    const double ms = std::chrono::duration<double, std::milli>(b - a).count();
    if (w == 1) t1 = ms;
    const double mips = static_cast<double>(N) / (ms / 1000.0) / 1e6;
    std::printf("workers=%-2u  %7.1f ms  %6.0f M items/s  speedup %4.2fx  [%s]\n", w, ms, mips, t1 / ms,
                g_total.load(std::memory_order_relaxed) == ref ? "OK" : "MISMATCH");
  }

  // Single-thread fallback (the web path when SharedArrayBuffer is unavailable):
  // no worker threads — the caller drives the queue. Must still be correct.
  {
    Scheduler s(1, Scheduler::Mode::Inline);
    g_total.store(0, std::memory_order_relaxed);
    const auto a = std::chrono::steady_clock::now();
    WaitGroup root;
    s.kick(sum_task(s, 0, N, grain), root);
    s.wait_idle();  // drives the queue on this thread
    const auto b = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(b - a).count();
    std::printf("inline      %7.1f ms  %6.0f M items/s  (web fallback)   [%s]\n", ms, static_cast<double>(N) / (ms / 1000.0) / 1e6,
                g_total.load(std::memory_order_relaxed) == ref ? "OK" : "MISMATCH");
  }
  return 0;
}
