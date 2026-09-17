#ifndef MOBAGEN_BENCHMARK_FRAME_V1_H
#define MOBAGEN_BENCHMARK_FRAME_V1_H

#include <mobagen/plugin/plugin_abi.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOBAGEN_BENCHMARK_FRAME_V1_ID "benchmark.frame.v1"

typedef uint64_t(MOBAGEN_PLUGIN_CALL* MobagenBenchmarkRunFrameFnV1)(void* plugin_state, uint32_t item_count);
typedef uint64_t(MOBAGEN_PLUGIN_CALL* MobagenBenchmarkFrameCountFnV1)(const void* plugin_state);

typedef struct MobagenBenchmarkFrameV1 {
  MobagenCapabilityHeaderV1 header;
  void* plugin_state;
  MobagenBenchmarkRunFrameFnV1 run_frame;
  MobagenBenchmarkFrameCountFnV1 frame_count;
} MobagenBenchmarkFrameV1;

#define MOBAGEN_BENCHMARK_FRAME_V1_SIZE ((uint32_t)sizeof(MobagenBenchmarkFrameV1))

/* Shared by the monolithic and plugin implementations so the benchmark isolates
   the module boundary instead of comparing different frame workloads. */
static inline uint64_t mobagen_benchmark_frame_workload(uint64_t accumulator, uint32_t item_count) {
  uint32_t index;
  for (index = 0; index < item_count; ++index) {
    accumulator ^= (uint64_t)index + UINT64_C(0x9e3779b97f4a7c15) + (accumulator << 6u) + (accumulator >> 2u);
    accumulator = (accumulator << 17u) | (accumulator >> 47u);
  }
  return accumulator;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MOBAGEN_BENCHMARK_FRAME_V1_H */
