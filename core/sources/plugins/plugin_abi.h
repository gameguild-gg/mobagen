#ifndef MOBAGEN_PLUGIN_ABI_H
#define MOBAGEN_PLUGIN_ABI_H

/*
 * Mobagen native plugin ABI v1.
 *
 * This contract is C-only on purpose. Plugin lifecycle calls are coarse and
 * capability services are published as versioned function tables, allowing
 * the frozen runtime graph to call service functions directly in hot paths.
 */

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  define MOBAGEN_PLUGIN_CALL __cdecl
#  if defined(MOBAGEN_PLUGIN_BUILD)
#    define MOBAGEN_PLUGIN_EXPORT __declspec(dllexport)
#  else
#    define MOBAGEN_PLUGIN_EXPORT
#  endif
#elif defined(MOBAGEN_PLUGIN_BUILD) && (defined(__GNUC__) || defined(__clang__))
#  define MOBAGEN_PLUGIN_CALL
#  define MOBAGEN_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#  define MOBAGEN_PLUGIN_CALL
#  define MOBAGEN_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MOBAGEN_PLUGIN_ABI_VERSION UINT32_C(1)
#define MOBAGEN_PLUGIN_ENTRY_V1_SYMBOL "mobagen_plugin_entry_v1"

typedef uint32_t MobagenStatus;
#define MOBAGEN_STATUS_OK UINT32_C(0)
#define MOBAGEN_STATUS_INVALID_ARGUMENT UINT32_C(1)
#define MOBAGEN_STATUS_UNSUPPORTED UINT32_C(2)
#define MOBAGEN_STATUS_FAILED UINT32_C(3)
#define MOBAGEN_STATUS_OUT_OF_MEMORY UINT32_C(4)
#define MOBAGEN_STATUS_CONFLICT UINT32_C(5)
#define MOBAGEN_STATUS_NOT_FOUND UINT32_C(6)

typedef uint32_t MobagenLogLevel;
#define MOBAGEN_LOG_TRACE UINT32_C(0)
#define MOBAGEN_LOG_DEBUG UINT32_C(1)
#define MOBAGEN_LOG_INFO UINT32_C(2)
#define MOBAGEN_LOG_WARNING UINT32_C(3)
#define MOBAGEN_LOG_ERROR UINT32_C(4)

typedef uint32_t MobagenReloadPolicy;
#define MOBAGEN_RELOAD_NEVER UINT32_C(0)
#define MOBAGEN_RELOAD_RESTART UINT32_C(1)
#define MOBAGEN_RELOAD_SAFE_POINT UINT32_C(2)

typedef struct MobagenStringView {
  const char* data;
  size_t size;
} MobagenStringView;

typedef struct MobagenByteView {
  const uint8_t* data;
  size_t size;
} MobagenByteView;

/* Every published capability function table starts with this header. */
typedef struct MobagenCapabilityHeaderV1 {
  uint32_t struct_size;
  uint32_t abi_version;
} MobagenCapabilityHeaderV1;

typedef void*(MOBAGEN_PLUGIN_CALL* MobagenAllocateFn)(void* host_context, size_t size, size_t alignment);
typedef void(MOBAGEN_PLUGIN_CALL* MobagenDeallocateFn)(void* host_context, void* memory, size_t size, size_t alignment);
typedef void(MOBAGEN_PLUGIN_CALL* MobagenLogFn)(void* host_context, MobagenLogLevel level, MobagenStringView message);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenPublishCapabilityFn)(void* host_context, MobagenStringView capability_id,
                                                                       uint32_t capability_abi_version, const void* function_table,
                                                                       uint32_t function_table_size);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenFindCapabilityFn)(void* host_context, MobagenStringView capability_id, uint32_t minimum_abi_version,
                                                                    const void** function_table, uint32_t* function_table_size);

typedef struct MobagenHostApiV1 {
  uint32_t struct_size;
  uint32_t abi_version;
  void* host_context;
  MobagenAllocateFn allocate;
  MobagenDeallocateFn deallocate;
  MobagenLogFn log;
  MobagenPublishCapabilityFn publish_capability;
  MobagenFindCapabilityFn find_capability;
} MobagenHostApiV1;

typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenPluginConfigureFn)(void* plugin_state, const MobagenHostApiV1* host, MobagenByteView configuration);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenPluginStartFn)(void* plugin_state);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenPluginQuiesceFn)(void* plugin_state);
typedef void(MOBAGEN_PLUGIN_CALL* MobagenPluginStopFn)(void* plugin_state);
typedef void(MOBAGEN_PLUGIN_CALL* MobagenPluginDestroyFn)(void* plugin_state);

typedef struct MobagenPluginLifecycleV1 {
  uint32_t struct_size;
  MobagenPluginConfigureFn configure;
  MobagenPluginStartFn start;
  MobagenPluginQuiesceFn quiesce;
  MobagenPluginStopFn stop;
  MobagenPluginDestroyFn destroy;
} MobagenPluginLifecycleV1;

typedef struct MobagenPluginDescriptorV1 {
  uint32_t struct_size;
  uint32_t abi_version;
  MobagenStringView id;
  uint32_t version_major;
  uint32_t version_minor;
  uint32_t version_patch;
  MobagenReloadPolicy reload_policy;
  void* plugin_state;
  const MobagenStringView* provides;
  uint32_t provides_count;
  const MobagenStringView* required;
  uint32_t required_count;
  const MobagenStringView* optional;
  uint32_t optional_count;
  const MobagenStringView* conflicts;
  uint32_t conflicts_count;
  MobagenPluginLifecycleV1 lifecycle;
} MobagenPluginDescriptorV1;

#define MOBAGEN_PLUGIN_HOST_API_V1_SIZE ((uint32_t)sizeof(MobagenHostApiV1))
#define MOBAGEN_PLUGIN_LIFECYCLE_V1_SIZE ((uint32_t)sizeof(MobagenPluginLifecycleV1))
#define MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE ((uint32_t)sizeof(MobagenPluginDescriptorV1))

typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenPluginEntryV1Fn)(const MobagenHostApiV1* host, MobagenPluginDescriptorV1* descriptor);

MOBAGEN_PLUGIN_EXPORT MobagenStatus MOBAGEN_PLUGIN_CALL mobagen_plugin_entry_v1(const MobagenHostApiV1* host, MobagenPluginDescriptorV1* descriptor);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MOBAGEN_PLUGIN_ABI_H */
