#include <mobagen/plugin/runtime_tick_v1.h>

#ifndef MOBAGEN_TEST_FAILURE_MODE
#  error "MOBAGEN_TEST_FAILURE_MODE must select the lifecycle failure"
#endif

typedef struct FailurePluginState {
  unsigned configured;
} FailurePluginState;

static FailurePluginState state;

static MobagenStatus MOBAGEN_PLUGIN_CALL tick(void* opaque) {
  (void)opaque;
  return MOBAGEN_STATUS_OK;
}

static uint64_t MOBAGEN_PLUGIN_CALL tick_count(const void* opaque) {
  (void)opaque;
  return 0;
}

static const MobagenRuntimeTickV1 tick_api = {
    .header = {sizeof(MobagenRuntimeTickV1), 1},
    .plugin_state = &state,
    .tick = tick,
    .tick_count = tick_count,
};

static MobagenStatus MOBAGEN_PLUGIN_CALL configure(void* opaque, const MobagenHostApiV1* host, MobagenByteView configuration) {
  static const char tick_id[] = MOBAGEN_RUNTIME_TICK_V1_ID;
  static const char mismatch_id[] = "runtime.undeclared.v1";
  FailurePluginState* plugin = (FailurePluginState*)opaque;
  const char* capability = MOBAGEN_TEST_FAILURE_MODE == 3 ? mismatch_id : tick_id;
  (void)configuration;
  if (plugin == NULL || host == NULL) return MOBAGEN_STATUS_INVALID_ARGUMENT;
  ++plugin->configured;
  if (host->publish_capability(host->host_context,
                               (MobagenStringView){capability, MOBAGEN_TEST_FAILURE_MODE == 3 ? sizeof(mismatch_id) - 1 : sizeof(tick_id) - 1}, 1,
                               &tick_api, sizeof(tick_api))
      != MOBAGEN_STATUS_OK) {
    return MOBAGEN_STATUS_FAILED;
  }
  return MOBAGEN_TEST_FAILURE_MODE == 1 ? MOBAGEN_STATUS_FAILED : MOBAGEN_STATUS_OK;
}

static MobagenStatus MOBAGEN_PLUGIN_CALL start(void* opaque) {
  if (opaque == NULL) return MOBAGEN_STATUS_INVALID_ARGUMENT;
  return MOBAGEN_TEST_FAILURE_MODE == 2 ? MOBAGEN_STATUS_FAILED : MOBAGEN_STATUS_OK;
}

static MobagenStatus MOBAGEN_PLUGIN_CALL quiesce(void* opaque) { return opaque == NULL ? MOBAGEN_STATUS_INVALID_ARGUMENT : MOBAGEN_STATUS_OK; }

static void MOBAGEN_PLUGIN_CALL stop(void* opaque) { (void)opaque; }

static void MOBAGEN_PLUGIN_CALL destroy(void* opaque) {
  FailurePluginState* plugin = (FailurePluginState*)opaque;
  if (plugin != NULL) plugin->configured = 0;
}

MOBAGEN_PLUGIN_EXPORT MobagenStatus MOBAGEN_PLUGIN_CALL mobagen_plugin_entry_v1(const MobagenHostApiV1* host, MobagenPluginDescriptorV1* descriptor) {
  static const MobagenStringView provides[] = {{MOBAGEN_RUNTIME_TICK_V1_ID, sizeof(MOBAGEN_RUNTIME_TICK_V1_ID) - 1}};
  if (host == NULL || descriptor == NULL || host->abi_version != MOBAGEN_PLUGIN_ABI_VERSION || host->struct_size < MOBAGEN_PLUGIN_HOST_API_V1_SIZE
      || descriptor->struct_size < MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE) {
    return MOBAGEN_STATUS_UNSUPPORTED;
  }
  *descriptor = (MobagenPluginDescriptorV1){
      .struct_size = MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE,
      .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
      .id = {"mobagen.lifecycle-failure", sizeof("mobagen.lifecycle-failure") - 1},
      .version_major = 1,
      .reload_policy = MOBAGEN_RELOAD_RESTART,
      .plugin_state = &state,
      .provides = provides,
      .provides_count = 1,
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
