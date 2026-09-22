/*
 * Reference guest for the BrowserWasmBackend smoke (dynamic-loading-all-
 * platforms todo 8). Compiled to standalone wasm by the emscripten web build
 * only (core/sources/plugins/browser/CMakeLists.txt) and embedded into the
 * MobagenBrowserBackendSmoke bundle as a byte array.
 *
 * Implements the full portable plugin contract (wasm_abi.h) so the loader
 * path — instantiate -> query descriptor -> activate (configure/start) —
 * exercises the real pipeline, PLUS one extra export the node smoke invokes
 * through the backend: mobagen_smoke_add(i32,i32)->i32.
 *
 * Also carries a module_abi.h annotation table (todo 1 macros) so the guest
 * stays forward-compatible with the todo 3 extraction tool / todo 10
 * marshaller. The plugin contract exports use fixed ABI symbols instead of
 * annotations; mobagen_smoke_add is the annotated representative.
 */
#include <mobagen/module/module_abi.h>
#include <mobagen/plugin/wasm_abi.h>

#include <stddef.h>
#include <stdint.h>

#if defined(__wasm__)
#  define MOBAGEN_WASM_GUEST_EXPORT __attribute__((visibility("default")))
#else
#  define MOBAGEN_WASM_GUEST_EXPORT
#endif

#define LINEAR_MEMORY_BYTES UINT32_C(65536) /* exactly one wasm page */
#define METADATA_BYTES UINT32_C(256)
#define MAX_ALLOCATIONS UINT32_C(8)

typedef struct Allocation {
  uint32_t offset;
  uint32_t size;
  uint32_t active;
} Allocation;

_Alignas(8) static uint8_t linear_memory[LINEAR_MEMORY_BYTES];
static Allocation allocations[MAX_ALLOCATIONS];
static uint32_t configured;
static uint32_t started;
static uint32_t metadata_ready;

static const char provider_id[] = "mobagen.browser-smoke.default";
static const char capability_id[] = "mobagen.smoke.v1";

static void copy_bytes(uint8_t* destination, const uint8_t* source, uint32_t size) {
  uint32_t index;
  for (index = 0; index < size; ++index) destination[index] = source[index];
}

static void zero_bytes(uint8_t* destination, uint32_t size) {
  uint32_t index;
  for (index = 0; index < size; ++index) destination[index] = 0;
}

static uint32_t guest_offset(const uint8_t* pointer) {
#if defined(__wasm__)
  return (uint32_t)(uintptr_t)pointer;
#else
  return (uint32_t)(pointer - linear_memory);
#endif
}

static uint8_t* guest_pointer(uint32_t offset, uint32_t size) {
  uintptr_t address;
  uintptr_t begin = (uintptr_t)linear_memory;
  uintptr_t end = begin + LINEAR_MEMORY_BYTES;
#if defined(__wasm__)
  address = (uintptr_t)offset;
#else
  if (offset > LINEAR_MEMORY_BYTES) return NULL;
  address = begin + offset;
#endif
  if (address < begin || address > end || size > end - address) return NULL;
  return (uint8_t*)address;
}

static uint32_t read_u32(const uint8_t* bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static void write_u32(uint8_t* bytes, uint32_t value) {
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8U);
  bytes[2] = (uint8_t)(value >> 16U);
  bytes[3] = (uint8_t)(value >> 24U);
}

static void ensure_metadata(void) {
  const uint32_t provider_offset = 8;
  const uint32_t capability_offset = 48;
  const uint32_t provides_offset = 88;
  if (metadata_ready) return;
  zero_bytes(linear_memory, METADATA_BYTES);
  copy_bytes(linear_memory + provider_offset, (const uint8_t*)provider_id, (uint32_t)(sizeof(provider_id) - 1U));
  copy_bytes(linear_memory + capability_offset, (const uint8_t*)capability_id, (uint32_t)(sizeof(capability_id) - 1U));
  write_u32(linear_memory + provides_offset, guest_offset(linear_memory + capability_offset));
  write_u32(linear_memory + provides_offset + 4, (uint32_t)(sizeof(capability_id) - 1U));
  metadata_ready = 1;
}

static uint32_t local_offset(uint32_t offset, uint32_t size) {
  uint8_t* pointer = guest_pointer(offset, size);
  return pointer == NULL ? UINT32_MAX : (uint32_t)(pointer - linear_memory);
}

static int overlaps(uint32_t offset, uint32_t size, const Allocation* allocation) {
  return allocation->active && offset < allocation->offset + allocation->size && allocation->offset < offset + size;
}

/* ---- module ABI v1 annotation table (todo 1 macros) ---- */

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_smoke_add(uint32_t a, uint32_t b) { return a + b; }

MOBAGEN_WASM_GUEST_EXPORT MobagenModuleStatus MOBAGEN_MODULE_CALL mobagen_module_entry_v1(const MobagenModuleHostApiV1* host,
                                                                                           MobagenModuleDescriptorV1* descriptor) {
  if (host == NULL || descriptor == NULL) return MOBAGEN_MODULE_STATUS_INVALID_ARGUMENT;
  if (host->struct_size < MOBAGEN_MODULE_HOST_API_V1_SIZE || host->abi_version != MOBAGEN_MODULE_ABI_VERSION) {
    return MOBAGEN_MODULE_STATUS_INVALID_ARGUMENT;
  }
  descriptor->struct_size = MOBAGEN_MODULE_DESCRIPTOR_V1_SIZE;
  descriptor->abi_version = MOBAGEN_MODULE_ABI_VERSION;
  descriptor->threads_policy = MOBAGEN_MODULE_THREADS_NONE;
  return MOBAGEN_MODULE_STATUS_OK;
}

MOBAGEN_MODULE_EXPORT_TABLE_BEGIN(MobagenBrowserSmokeExports, 1)
MOBAGEN_MODULE_EXPORT_ENTRY(MobagenBrowserSmokeExports, mobagen_smoke_add, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32,
                            MOBAGEN_MODULE_T_I32)
MOBAGEN_MODULE_EXPORT_TABLE_END(MobagenBrowserSmokeExports, mobagen_smoke_add);

/* The annotation table must be discoverable under the canonical symbol. */
const MobagenModuleExportTableV1* MOBAGEN_WASM_GUEST_EXPORT mobagen_module_exports_v1 = (const MobagenModuleExportTableV1*)&MobagenBrowserSmokeExports;

/* ---- portable plugin contract (wasm_abi.h) ---- */

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_allocate_v1(uint32_t size, uint32_t alignment) {
  uint32_t slot;
  uint32_t candidate;
  uint32_t occupied;
  if (size == 0) return MOBAGEN_WASM_NULL_OFFSET;
  if (alignment == 0 || alignment > MOBAGEN_WASM_EXCHANGE_ALIGNMENT || (alignment & (alignment - 1U)) != 0
      || size > LINEAR_MEMORY_BYTES - METADATA_BYTES) {
    return MOBAGEN_WASM_NULL_OFFSET;
  }
  ensure_metadata();
  for (slot = 0; slot < MAX_ALLOCATIONS && allocations[slot].active; ++slot) {
  }
  if (slot == MAX_ALLOCATIONS) return MOBAGEN_WASM_NULL_OFFSET;
  for (candidate = METADATA_BYTES; candidate <= LINEAR_MEMORY_BYTES - size; candidate += MOBAGEN_WASM_EXCHANGE_ALIGNMENT) {
    uint32_t index;
    occupied = 0;
    for (index = 0; index < MAX_ALLOCATIONS; ++index) {
      if (overlaps(candidate, size, &allocations[index])) {
        occupied = 1;
        break;
      }
    }
    if (!occupied) {
      allocations[slot].offset = candidate;
      allocations[slot].size = size;
      allocations[slot].active = 1;
      return guest_offset(linear_memory + candidate);
    }
  }
  return MOBAGEN_WASM_NULL_OFFSET;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_deallocate_v1(uint32_t offset, uint32_t size, uint32_t alignment) {
  uint32_t index;
  uint32_t local;
  if (alignment == 0 || alignment > MOBAGEN_WASM_EXCHANGE_ALIGNMENT || (alignment & (alignment - 1U)) != 0) {
    return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  }
  local = local_offset(offset, size);
  if (local == UINT32_MAX) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  for (index = 0; index < MAX_ALLOCATIONS; ++index) {
    if (allocations[index].active && allocations[index].offset == local && allocations[index].size == size) {
      allocations[index].active = 0;
      return MOBAGEN_WASM_STATUS_OK;
    }
  }
  return MOBAGEN_WASM_STATUS_NOT_FOUND;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_query_v1(uint32_t descriptor_offset, uint32_t descriptor_capacity) {
  uint8_t* descriptor;
  ensure_metadata();
  if (descriptor_capacity < MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE) {
    return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  }
  descriptor = guest_pointer(descriptor_offset, MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
  if (descriptor == NULL) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  zero_bytes(descriptor, MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
  write_u32(descriptor, MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
  write_u32(descriptor + 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
  write_u32(descriptor + 8, guest_offset(linear_memory + 8));
  write_u32(descriptor + 12, (uint32_t)(sizeof(provider_id) - 1U));
  write_u32(descriptor + 16, 1); /* version 1.0.0 */
  write_u32(descriptor + 20, 0);
  write_u32(descriptor + 24, 0);
  write_u32(descriptor + 28, MOBAGEN_WASM_RELOAD_RESTART);
  write_u32(descriptor + 32, guest_offset(linear_memory + 88)); /* provides array */
  write_u32(descriptor + 36, 1);
  return MOBAGEN_WASM_STATUS_OK;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_configure_v1(uint32_t configuration_offset, uint32_t configuration_size) {
  if (started) return MOBAGEN_WASM_STATUS_CONFLICT;
  if (configuration_size != 0 && guest_pointer(configuration_offset, configuration_size) == NULL) {
    return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  }
  configured = 1;
  return MOBAGEN_WASM_STATUS_OK;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_start_v1(void) {
  if (!configured || started) return MOBAGEN_WASM_STATUS_CONFLICT;
  started = 1;
  return MOBAGEN_WASM_STATUS_OK;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_quiesce_v1(void) {
  if (!started) return MOBAGEN_WASM_STATUS_CONFLICT;
  started = 0;
  return MOBAGEN_WASM_STATUS_OK;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_stop_v1(void) {
  configured = 0;
  started = 0;
  return MOBAGEN_WASM_STATUS_OK;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_process_v1(uint32_t input_batch_offset, uint32_t output_batch_offset,
                                                                  uint32_t result_offset) {
  /* The smoke never submits command batches; decline politely. */
  (void)input_batch_offset;
  (void)output_batch_offset;
  (void)result_offset;
  if (!started) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  return MOBAGEN_WASM_STATUS_UNSUPPORTED;
}
