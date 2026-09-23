/*
 * Memory alloc/GC micro-benchmark (dynamic-loading-all-platforms todo 15).
 *
 * Measures alloc throughput and GC throughput on the NATIVE shim
 * (standalone NativeShim — owns a 256 KiB region, no WAMR needed).
 * Outputs ops/sec lines consumed by evidence logs.
 *
 * Also runnable through the web smoke via a benchmark section in
 * smoke_main.cpp (todo 15 web path).
 */

#include "memory/memory_manager.hpp"
#include "memory/native_shim.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

  constexpr std::size_t kRegionBytes = 512U * 1024U;
  constexpr std::uint32_t kPayloadSmall = 32;
  constexpr std::uint32_t kPayloadMedium = 128;
  constexpr std::uint32_t kAllocsPerSample = 10000;
  constexpr std::uint32_t kCollectsPerSample = 1000;

  double median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
  }

  struct BenchResult {
    const char* name;
    double ops_per_sec;
    double median_ns;
  };

  BenchResult bench_alloc(const char* name, std::uint32_t payload_bytes, std::size_t warmup, std::size_t samples) {
    std::vector<double> times;
    times.reserve(samples);

    for (std::size_t w = 0; w < warmup; ++w) {
      auto shim = mobagen::memory::make_native_shim();
      mobagen::memory::MemoryManager mgr{shim->shim()};
      (void)mgr.init(kRegionBytes);
      for (std::uint32_t i = 0; i < kAllocsPerSample; ++i) {
        (void)mgr.alloc(payload_bytes, 0);
      }
    }

    for (std::size_t s = 0; s < samples; ++s) {
      auto shim = mobagen::memory::make_native_shim();
      mobagen::memory::MemoryManager mgr{shim->shim()};
      (void)mgr.init(kRegionBytes);
      const auto t0 = std::chrono::steady_clock::now();
      for (std::uint32_t i = 0; i < kAllocsPerSample; ++i) {
        (void)mgr.alloc(payload_bytes, 0);
      }
      const auto t1 = std::chrono::steady_clock::now();
      times.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    const double med = median(times);
    const double ops_sec = static_cast<double>(kAllocsPerSample) / (med * 1e-9);
    return {name, ops_sec, med};
  }

  BenchResult bench_collect(const char* name, std::uint32_t payload_bytes, std::size_t warmup, std::size_t samples) {
    std::vector<double> times;
    times.reserve(samples);

    for (std::size_t w = 0; w < warmup; ++w) {
      auto shim = mobagen::memory::make_native_shim();
      mobagen::memory::MemoryManager mgr{shim->shim()};
      (void)mgr.init(kRegionBytes);
      for (std::uint32_t i = 0; i < kAllocsPerSample; ++i) (void)mgr.alloc(payload_bytes, 0);
      for (std::uint32_t i = 0; i < kCollectsPerSample; ++i) (void)mgr.collect();
    }

    for (std::size_t s = 0; s < samples; ++s) {
      auto shim = mobagen::memory::make_native_shim();
      mobagen::memory::MemoryManager mgr{shim->shim()};
      (void)mgr.init(kRegionBytes);
      for (std::uint32_t i = 0; i < kAllocsPerSample; ++i) (void)mgr.alloc(payload_bytes, 0);
      const auto t0 = std::chrono::steady_clock::now();
      for (std::uint32_t i = 0; i < kCollectsPerSample; ++i) (void)mgr.collect();
      const auto t1 = std::chrono::steady_clock::now();
      times.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    const double med = median(times);
    const double ops_sec = static_cast<double>(kCollectsPerSample) / (med * 1e-9);
    return {name, ops_sec, med};
  }

  BenchResult bench_alloc_collect_cycle(const char* name, std::uint32_t payload_bytes, std::size_t warmup, std::size_t samples) {
    constexpr std::uint32_t kBatchAllocs = 500;
    constexpr std::uint32_t kCycles = 200;
    std::vector<double> times;
    times.reserve(samples);

    for (std::size_t w = 0; w < warmup; ++w) {
      auto shim = mobagen::memory::make_native_shim();
      mobagen::memory::MemoryManager mgr{shim->shim()};
      (void)mgr.init(kRegionBytes);
      for (std::uint32_t c = 0; c < kCycles; ++c) {
        for (std::uint32_t i = 0; i < kBatchAllocs; ++i) (void)mgr.alloc(payload_bytes, 0);
        (void)mgr.collect();
      }
    }

    for (std::size_t s = 0; s < samples; ++s) {
      auto shim = mobagen::memory::make_native_shim();
      mobagen::memory::MemoryManager mgr{shim->shim()};
      (void)mgr.init(kRegionBytes);
      const auto t0 = std::chrono::steady_clock::now();
      for (std::uint32_t c = 0; c < kCycles; ++c) {
        for (std::uint32_t i = 0; i < kBatchAllocs; ++i) (void)mgr.alloc(payload_bytes, 0);
        (void)mgr.collect();
      }
      const auto t1 = std::chrono::steady_clock::now();
      times.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    const double total_ops = static_cast<double>(kCycles * (kBatchAllocs + 1));
    const double med = median(times);
    const double ops_sec = total_ops / (med * 1e-9);
    return {name, ops_sec, med};
  }

  void print_result(const BenchResult& r) {
    std::printf("[mem-bench] %s median=%.0f ns ops/sec=%.0f\n", r.name, r.median_ns, r.ops_per_sec);
  }

}  // namespace

int main(int argc, char** argv) {
  std::size_t warmup = 5;
  std::size_t samples = 30;

  for (int i = 1; i < argc; ++i) {
    if (i + 1 < argc && std::string(argv[i]) == "--warmup") warmup = static_cast<std::size_t>(std::atoi(argv[i + 1]));
    if (i + 1 < argc && std::string(argv[i]) == "--samples") samples = static_cast<std::size_t>(std::atoi(argv[i + 1]));
    ++i;
  }

  std::printf("[mem-bench] native-standalone warmup=%zu samples=%zu\n", warmup, samples);

  auto r1 = bench_alloc("alloc-32B", kPayloadSmall, warmup, samples);
  print_result(r1);

  auto r2 = bench_alloc("alloc-128B", kPayloadMedium, warmup, samples);
  print_result(r2);

  auto r3 = bench_collect("collect-after-10k-allocs-32B", kPayloadSmall, warmup, samples);
  print_result(r3);

  auto r4 = bench_collect("collect-after-10k-allocs-128B", kPayloadMedium, warmup, samples);
  print_result(r4);

  auto r5 = bench_alloc_collect_cycle("alloc+collect-cycle-32B", kPayloadSmall, warmup, samples);
  print_result(r5);

  auto r6 = bench_alloc_collect_cycle("alloc+collect-cycle-128B", kPayloadMedium, warmup, samples);
  print_result(r6);

  std::printf("[mem-bench] done\n");
  return 0;
}
