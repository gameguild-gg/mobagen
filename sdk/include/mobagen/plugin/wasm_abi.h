#ifndef MOBAGEN_PLUGIN_WASM_ABI_H
#define MOBAGEN_PLUGIN_WASM_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Portable WebAssembly plugin ABI.
 *
 * Every offset is relative to the guest's 32-bit linear memory. Hosts must
 * bounds-check offset + size without wrapping before reading or writing. No
 * native pointer, size_t, allocator, STL type, or compiler-specific layout
 * crosses this boundary.
 */

#define MOBAGEN_WASM_PLUGIN_ABI_VERSION 1U
#define MOBAGEN_WASM_LINEAR_MEMORY_PAGE_BYTES 65536U
#define MOBAGEN_WASM_COMMAND_ALIGNMENT 8U
#define MOBAGEN_WASM_MAX_STRING_BYTES 4096U
#define MOBAGEN_WASM_MAX_DESCRIPTOR_ENTRIES 1024U
#define MOBAGEN_WASM_MAX_CONFIGURATION_BYTES 1048576U
#define MOBAGEN_WASM_MAX_COMMAND_BATCH_BYTES 16777216U

#define MOBAGEN_WASM_STATUS_OK 0U
#define MOBAGEN_WASM_STATUS_INVALID_ARGUMENT 1U
#define MOBAGEN_WASM_STATUS_UNSUPPORTED 2U
#define MOBAGEN_WASM_STATUS_NOT_FOUND 3U
#define MOBAGEN_WASM_STATUS_CONFLICT 4U
#define MOBAGEN_WASM_STATUS_OUT_OF_MEMORY 5U
#define MOBAGEN_WASM_STATUS_FAILED 6U

#define MOBAGEN_WASM_RELOAD_NEVER 0U
#define MOBAGEN_WASM_RELOAD_RESTART 1U
#define MOBAGEN_WASM_RELOAD_SAFE_POINT 2U

/* Canonical import module and field names exposed by the host. */
#define MOBAGEN_WASM_IMPORT_MODULE_V1 "mobagen_v1"
#define MOBAGEN_WASM_IMPORT_LOG_V1 "log"
#define MOBAGEN_WASM_IMPORT_FIND_CAPABILITY_V1 "find_capability"
#define MOBAGEN_WASM_IMPORT_SUBMIT_COMMANDS_V1 "submit_commands"

/* Canonical function exports required from a portable guest. */
#define MOBAGEN_WASM_EXPORT_QUERY_V1 "mobagen_wasm_plugin_query_v1"
#define MOBAGEN_WASM_EXPORT_CONFIGURE_V1 "mobagen_wasm_plugin_configure_v1"
#define MOBAGEN_WASM_EXPORT_START_V1 "mobagen_wasm_plugin_start_v1"
#define MOBAGEN_WASM_EXPORT_QUIESCE_V1 "mobagen_wasm_plugin_quiesce_v1"
#define MOBAGEN_WASM_EXPORT_STOP_V1 "mobagen_wasm_plugin_stop_v1"
#define MOBAGEN_WASM_EXPORT_PROCESS_V1 "mobagen_wasm_plugin_process_v1"

typedef struct MobagenWasmSpan32 {
  uint32_t offset;
  uint32_t size;
} MobagenWasmSpan32;

typedef struct MobagenWasmArray32 {
  uint32_t offset;
  uint32_t count;
} MobagenWasmArray32;

/* Complete generational identity; zero generation is invalid. */
typedef struct MobagenWasmHandle32 {
  uint32_t index;
  uint32_t generation;
} MobagenWasmHandle32;

/* Arrays of strings contain contiguous MobagenWasmSpan32 entries. */
typedef struct MobagenWasmPluginDescriptorV1 {
  uint32_t struct_size;
  uint32_t abi_version;
  MobagenWasmSpan32 id;
  uint32_t version_major;
  uint32_t version_minor;
  uint32_t version_patch;
  uint32_t reload_policy;
  MobagenWasmArray32 provides;
  MobagenWasmArray32 required;
  MobagenWasmArray32 optional;
  MobagenWasmArray32 conflicts;
  MobagenWasmSpan32 configuration_schema;
  MobagenWasmArray32 permissions;
} MobagenWasmPluginDescriptorV1;

/* Each encoded command starts with this header and occupies byte_size bytes. */
typedef struct MobagenWasmCommandHeaderV1 {
  uint32_t byte_size;
  uint32_t opcode;
} MobagenWasmCommandHeaderV1;

/* One boundary call carries many aligned commands to amortize call overhead. */
typedef struct MobagenWasmCommandBatchV1 {
  uint32_t struct_size;
  uint32_t abi_version;
  MobagenWasmSpan32 bytes;
  uint32_t command_count;
  uint32_t reserved;
} MobagenWasmCommandBatchV1;

typedef struct MobagenWasmCommandResultV1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t status;
  uint32_t bytes_written;
  uint32_t commands_written;
  uint32_t reserved;
} MobagenWasmCommandResultV1;

#define MOBAGEN_WASM_SPAN32_SIZE ((uint32_t)sizeof(MobagenWasmSpan32))
#define MOBAGEN_WASM_ARRAY32_SIZE ((uint32_t)sizeof(MobagenWasmArray32))
#define MOBAGEN_WASM_HANDLE32_SIZE ((uint32_t)sizeof(MobagenWasmHandle32))
#define MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE ((uint32_t)sizeof(MobagenWasmPluginDescriptorV1))
#define MOBAGEN_WASM_COMMAND_HEADER_V1_SIZE ((uint32_t)sizeof(MobagenWasmCommandHeaderV1))
#define MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE ((uint32_t)sizeof(MobagenWasmCommandBatchV1))
#define MOBAGEN_WASM_COMMAND_RESULT_V1_SIZE ((uint32_t)sizeof(MobagenWasmCommandResultV1))

/*
 * Scalar WebAssembly signatures used by the canonical exports. Structures are
 * exchanged through validated linear-memory offsets, never passed by value.
 */
typedef uint32_t (*MobagenWasmPluginQueryV1Fn)(uint32_t descriptor_offset, uint32_t descriptor_capacity);
typedef uint32_t (*MobagenWasmPluginConfigureV1Fn)(uint32_t configuration_offset, uint32_t configuration_size);
typedef uint32_t (*MobagenWasmPluginLifecycleV1Fn)(void);
typedef uint32_t (*MobagenWasmPluginProcessV1Fn)(uint32_t input_batch_offset, uint32_t output_batch_offset, uint32_t result_offset);
typedef uint32_t (*MobagenWasmHostLogV1Fn)(uint32_t level, uint32_t message_offset, uint32_t message_size);
typedef uint32_t (*MobagenWasmHostFindCapabilityV1Fn)(uint32_t capability_offset, uint32_t capability_size, uint32_t capability_version,
                                                      uint32_t output_handle_offset);
typedef uint32_t (*MobagenWasmHostSubmitCommandsV1Fn)(uint32_t input_batch_offset, uint32_t result_offset);

#ifdef __cplusplus
}
#endif

#endif /* MOBAGEN_PLUGIN_WASM_ABI_H */
