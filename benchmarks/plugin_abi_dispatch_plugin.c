#include <mobagen/plugin/runtime_tick_v1.h>

#include "benchmark_frame_v1.h"

#include <stddef.h>

typedef struct BenchmarkPluginState {
  unsigned configured;
  unsigned started;
  uint64_t ticks;
  uint64_t frames;
  uint64_t accumulator;
} BenchmarkPluginState;

static BenchmarkPluginState state;

static MobagenStatus MOBAGEN_PLUGIN_CALL tick(void* opaque) {
  BenchmarkPluginState* plugin = (BenchmarkPluginState*)opaque;
  if (plugin == NULL || plugin->started == 0) return MOBAGEN_STATUS_CONFLICT;
  ++plugin->ticks;
  return MOBAGEN_STATUS_OK;
}

static uint64_t MOBAGEN_PLUGIN_CALL tick_count(const void* opaque) {
  const BenchmarkPluginState* plugin = (const BenchmarkPluginState*)opaque;
  return plugin == NULL ? 0 : plugin->ticks;
}

static const MobagenRuntimeTickV1 tick_api = {
    .header = {MOBAGEN_RUNTIME_TICK_V1_SIZE, 1},
    .plugin_state = &state,
    .tick = tick,
    .tick_count = tick_count,
};

static uint64_t MOBAGEN_PLUGIN_CALL run_frame(void* opaque, uint32_t item_count) {
  BenchmarkPluginState* plugin = (BenchmarkPluginState*)opaque;
  if (plugin == NULL || plugin->started == 0) return 0;
  plugin->accumulator = mobagen_benchmark_frame_workload(plugin->accumulator ^ plugin->frames, item_count);
  ++plugin->frames;
  return plugin->accumulator;
}

static uint64_t MOBAGEN_PLUGIN_CALL frame_count(const void* opaque) {
  const BenchmarkPluginState* plugin = (const BenchmarkPluginState*)opaque;
  return plugin == NULL ? 0 : plugin->frames;
}

static const MobagenBenchmarkFrameV1 frame_api = {
    .header = {MOBAGEN_BENCHMARK_FRAME_V1_SIZE, 1},
    .plugin_state = &state,
    .run_frame = run_frame,
    .frame_count = frame_count,
};

static MobagenStatus MOBAGEN_PLUGIN_CALL configure(void* opaque, const MobagenHostApiV1* host, MobagenByteView configuration) {
  BenchmarkPluginState* plugin = (BenchmarkPluginState*)opaque;
  (void)configuration;
  if (plugin == NULL || host == NULL) return MOBAGEN_STATUS_INVALID_ARGUMENT;
  ++plugin->configured;
  plugin->ticks = 0;
  plugin->frames = 0;
  plugin->accumulator = UINT64_C(0xcbf29ce484222325);
  if (host->publish_capability(host->host_context, (MobagenStringView){MOBAGEN_RUNTIME_TICK_V1_ID, sizeof(MOBAGEN_RUNTIME_TICK_V1_ID) - 1}, 1,
                               &tick_api, sizeof(tick_api))
      != MOBAGEN_STATUS_OK) {
    return MOBAGEN_STATUS_FAILED;
  }
  return host->publish_capability(host->host_context, (MobagenStringView){MOBAGEN_BENCHMARK_FRAME_V1_ID, sizeof(MOBAGEN_BENCHMARK_FRAME_V1_ID) - 1},
                                  1, &frame_api, sizeof(frame_api));
}

static MobagenStatus MOBAGEN_PLUGIN_CALL start(void* opaque) {
  BenchmarkPluginState* plugin = (BenchmarkPluginState*)opaque;
  if (plugin == NULL || plugin->configured == 0) return MOBAGEN_STATUS_FAILED;
  plugin->started = 1;
  return MOBAGEN_STATUS_OK;
}

static MobagenStatus MOBAGEN_PLUGIN_CALL quiesce(void* opaque) {
  BenchmarkPluginState* plugin = (BenchmarkPluginState*)opaque;
  if (plugin == NULL) return MOBAGEN_STATUS_INVALID_ARGUMENT;
  plugin->started = 0;
  return MOBAGEN_STATUS_OK;
}

static void MOBAGEN_PLUGIN_CALL stop(void* opaque) {
  BenchmarkPluginState* plugin = (BenchmarkPluginState*)opaque;
  if (plugin != NULL) plugin->started = 0;
}

static void MOBAGEN_PLUGIN_CALL destroy(void* opaque) {
  BenchmarkPluginState* plugin = (BenchmarkPluginState*)opaque;
  if (plugin != NULL) {
    plugin->configured = 0;
    plugin->started = 0;
    plugin->ticks = 0;
    plugin->frames = 0;
    plugin->accumulator = 0;
  }
}

MOBAGEN_PLUGIN_EXPORT MobagenStatus MOBAGEN_PLUGIN_CALL mobagen_plugin_entry_v1(const MobagenHostApiV1* host, MobagenPluginDescriptorV1* descriptor) {
  static const MobagenStringView provides[] = {
      {MOBAGEN_RUNTIME_TICK_V1_ID, sizeof(MOBAGEN_RUNTIME_TICK_V1_ID) - 1},
      {MOBAGEN_BENCHMARK_FRAME_V1_ID, sizeof(MOBAGEN_BENCHMARK_FRAME_V1_ID) - 1},
  };
  if (host == NULL || descriptor == NULL || host->abi_version != MOBAGEN_PLUGIN_ABI_VERSION || host->struct_size < MOBAGEN_PLUGIN_HOST_API_V1_SIZE
      || descriptor->struct_size < MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE) {
    return MOBAGEN_STATUS_UNSUPPORTED;
  }

  *descriptor = (MobagenPluginDescriptorV1){
      .struct_size = MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE,
      .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
      .id = {"mobagen.benchmark-abi", sizeof("mobagen.benchmark-abi") - 1},
      .version_major = 1,
      .version_minor = 0,
      .version_patch = 0,
      .reload_policy = MOBAGEN_RELOAD_RESTART,
      .plugin_state = &state,
      .provides = provides,
      .provides_count = 2,
      .required = NULL,
      .required_count = 0,
      .optional = NULL,
      .optional_count = 0,
      .conflicts = NULL,
      .conflicts_count = 0,
      .lifecycle =
          {
              .struct_size = MOBAGEN_PLUGIN_LIFECYCLE_V1_SIZE,
              .configure = configure,
              .start = start,
              .quiesce = quiesce,
              .stop = stop,
              .destroy = destroy,
          },
  };
  return MOBAGEN_STATUS_OK;
}
