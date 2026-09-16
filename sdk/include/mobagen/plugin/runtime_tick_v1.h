#ifndef MOBAGEN_RUNTIME_TICK_V1_H
#define MOBAGEN_RUNTIME_TICK_V1_H

#include "plugin_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOBAGEN_RUNTIME_TICK_V1_ID "runtime.tick.v1"

typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenRuntimeTickFnV1)(void* plugin_state);
typedef uint64_t(MOBAGEN_PLUGIN_CALL* MobagenRuntimeTickCountFnV1)(const void* plugin_state);

typedef struct MobagenRuntimeTickV1 {
  MobagenCapabilityHeaderV1 header;
  void* plugin_state;
  MobagenRuntimeTickFnV1 tick;
  MobagenRuntimeTickCountFnV1 tick_count;
} MobagenRuntimeTickV1;

#define MOBAGEN_RUNTIME_TICK_V1_SIZE ((uint32_t)sizeof(MobagenRuntimeTickV1))

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MOBAGEN_RUNTIME_TICK_V1_H */
