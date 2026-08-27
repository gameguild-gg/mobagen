// Uses ONLY the C ABI facade (jobs_c.h) — no Scheduler/Task/WaitGroup includes.
// Demonstrates the module boundary: a caller drives the parallel job system
// through a stable C contract, the way the renderer / a script / another language
// would. Sums [0,N) in parallel and verifies the result.

#include "jobs_c.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

static std::atomic<std::uint64_t> g_sum{0};

static void work(size_t begin, size_t end, void* /*user*/) {
  std::uint64_t acc = 0;
  for (size_t i = begin; i < end; ++i) acc += i;
  g_sum.fetch_add(acc, std::memory_order_relaxed);
}

int main() {
  JobSystem* js = jobs_create(0);  // 0 => hardware_concurrency
  std::printf("jobs_c: %u workers\n", jobs_worker_count(js));

  const size_t N = 10'000'000;
  jobs_parallel_for(js, N, 65536, work, nullptr);

  const std::uint64_t expected = static_cast<std::uint64_t>(N - 1) * N / 2;
  const std::uint64_t got = g_sum.load(std::memory_order_relaxed);
  std::printf("parallel_for sum = %llu  expected = %llu  [%s]\n", static_cast<unsigned long long>(got), static_cast<unsigned long long>(expected),
              got == expected ? "OK" : "MISMATCH");

  jobs_destroy(js);
  return 0;
}
