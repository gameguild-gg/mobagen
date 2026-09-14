#include "plugins/plugin_abi.h"

#include <stddef.h>

typedef struct ReferencePluginState {
  unsigned configured;
  unsigned started;
} ReferencePluginState;

static ReferencePluginState state;

static MobagenStatus MOBAGEN_PLUGIN_CALL configure(void* opaque, const MobagenHostApiV1* host, MobagenByteView configuration) {
  ReferencePluginState* plugin = (ReferencePluginState*)opaque;
  (void)configuration;
  if (plugin == NULL || host == NULL) return MOBAGEN_STATUS_INVALID_ARGUMENT;
  ++plugin->configured;
  return MOBAGEN_STATUS_OK;
}

static MobagenStatus MOBAGEN_PLUGIN_CALL start(void* opaque) {
  ReferencePluginState* plugin = (ReferencePluginState*)opaque;
  if (plugin == NULL || plugin->configured == 0) return MOBAGEN_STATUS_FAILED;
  ++plugin->started;
  return MOBAGEN_STATUS_OK;
}

static MobagenStatus MOBAGEN_PLUGIN_CALL quiesce(void* opaque) { return opaque == NULL ? MOBAGEN_STATUS_INVALID_ARGUMENT : MOBAGEN_STATUS_OK; }

static void MOBAGEN_PLUGIN_CALL stop(void* opaque) {
  ReferencePluginState* plugin = (ReferencePluginState*)opaque;
  if (plugin != NULL) plugin->started = 0;
}

static void MOBAGEN_PLUGIN_CALL destroy(void* opaque) {
  ReferencePluginState* plugin = (ReferencePluginState*)opaque;
  if (plugin != NULL) {
    plugin->configured = 0;
    plugin->started = 0;
  }
}

MOBAGEN_PLUGIN_EXPORT MobagenStatus MOBAGEN_PLUGIN_CALL mobagen_plugin_entry_v1(const MobagenHostApiV1* host, MobagenPluginDescriptorV1* descriptor) {
  static const MobagenStringView provides[] = {{"runtime.tick.v1", sizeof("runtime.tick.v1") - 1}};
  if (host == NULL || descriptor == NULL || host->abi_version != MOBAGEN_PLUGIN_ABI_VERSION || host->struct_size < MOBAGEN_PLUGIN_HOST_API_V1_SIZE
      || descriptor->struct_size < MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE) {
    return MOBAGEN_STATUS_UNSUPPORTED;
  }

  *descriptor = (MobagenPluginDescriptorV1){
      .struct_size = MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE,
      .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
      .id = {"mobagen.reference", sizeof("mobagen.reference") - 1},
      .version_major = 1,
      .version_minor = 0,
      .version_patch = 0,
      .reload_policy = MOBAGEN_RELOAD_RESTART,
      .plugin_state = &state,
      .provides = provides,
      .provides_count = 1,
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
