#include <mobagen/plugin/wasm_asset_store_v1.h>

#include <stddef.h>
#include <stdint.h>

#if defined(__wasm__)
#  define MOBAGEN_WASM_GUEST_EXPORT __attribute__((visibility("default")))
#else
#  define MOBAGEN_WASM_GUEST_EXPORT
#endif

#define LINEAR_MEMORY_BYTES UINT32_C(524288)
#define METADATA_BYTES UINT32_C(256)
#define MAX_ALLOCATIONS UINT32_C(16)

typedef struct Allocation {
  uint32_t offset;
  uint32_t size;
  uint32_t active;
} Allocation;

typedef struct AssetSlot {
  MobagenWasmAssetIdV1 id;
  uint32_t payload_offset;
  uint32_t payload_size;
  uint32_t generation;
  uint32_t leases;
  uint32_t occupied;
} AssetSlot;

_Alignas(8) static uint8_t linear_memory[LINEAR_MEMORY_BYTES];
static uint8_t asset_payloads[MOBAGEN_WASM_ASSET_MAX_CONFIGURED_BYTES];
static Allocation allocations[MAX_ALLOCATIONS];
static AssetSlot assets[MOBAGEN_WASM_ASSET_MAX_CONFIGURED_ASSETS];
static uint32_t asset_count;
static uint32_t payload_bytes;
static uint32_t metadata_ready;
static uint32_t configured;
static uint32_t started;

static const char provider_id[] = "mobagen.assets.default";
static const char capability_id[] = MOBAGEN_WASM_ASSET_STORE_V1_ID;
static const char configuration_schema[] = "mobagen.assets.portable.config.v1";

static uint32_t align_command(uint32_t value) { return (value + MOBAGEN_WASM_COMMAND_ALIGNMENT - 1U) & ~(MOBAGEN_WASM_COMMAND_ALIGNMENT - 1U); }

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

static int same_id(const MobagenWasmAssetIdV1* left, const MobagenWasmAssetIdV1* right) {
  uint32_t index;
  uint8_t difference = 0;
  for (index = 0; index < MOBAGEN_WASM_ASSET_ID_BYTES; ++index) {
    difference |= left->bytes[index] ^ right->bytes[index];
  }
  return difference == 0;
}

static void ensure_metadata(void) {
  const uint32_t provider_offset = 8;
  const uint32_t capability_offset = 40;
  const uint32_t provides_offset = 64;
  const uint32_t schema_offset = 80;
  if (metadata_ready) return;
  zero_bytes(linear_memory, METADATA_BYTES);
  copy_bytes(linear_memory + provider_offset, (const uint8_t*)provider_id, (uint32_t)(sizeof(provider_id) - 1U));
  copy_bytes(linear_memory + capability_offset, (const uint8_t*)capability_id, (uint32_t)(sizeof(capability_id) - 1U));
  write_u32(linear_memory + provides_offset, guest_offset(linear_memory + capability_offset));
  write_u32(linear_memory + provides_offset + 4, (uint32_t)(sizeof(capability_id) - 1U));
  copy_bytes(linear_memory + schema_offset, (const uint8_t*)configuration_schema, (uint32_t)(sizeof(configuration_schema) - 1U));
  metadata_ready = 1;
}

static uint32_t local_offset(uint32_t offset, uint32_t size) {
  uint8_t* pointer = guest_pointer(offset, size);
  return pointer == NULL ? UINT32_MAX : (uint32_t)(pointer - linear_memory);
}

static int overlaps(uint32_t offset, uint32_t size, const Allocation* allocation) {
  return allocation->active && offset < allocation->offset + allocation->size && allocation->offset < offset + size;
}

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
  write_u32(descriptor + 16, 1);
  write_u32(descriptor + 20, 0);
  write_u32(descriptor + 24, 0);
  write_u32(descriptor + 28, MOBAGEN_WASM_RELOAD_RESTART);
  write_u32(descriptor + 32, guest_offset(linear_memory + 64));
  write_u32(descriptor + 36, 1);
  write_u32(descriptor + 64, guest_offset(linear_memory + 80));
  write_u32(descriptor + 68, (uint32_t)(sizeof(configuration_schema) - 1U));
  return MOBAGEN_WASM_STATUS_OK;
}

static void reset_assets(void) {
  uint32_t index;
  for (index = 0; index < MOBAGEN_WASM_ASSET_MAX_CONFIGURED_ASSETS; ++index) {
    assets[index].generation = assets[index].generation == UINT32_MAX ? 1 : assets[index].generation + 1U;
    if (assets[index].generation == 0) assets[index].generation = 1;
    assets[index].payload_offset = 0;
    assets[index].payload_size = 0;
    assets[index].leases = 0;
    assets[index].occupied = 0;
  }
  asset_count = 0;
  payload_bytes = 0;
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_configure_v1(uint32_t configuration_offset, uint32_t configuration_size) {
  uint8_t* configuration;
  uint32_t count;
  uint32_t cursor;
  uint32_t index;
  if (started) return MOBAGEN_WASM_STATUS_CONFLICT;
  reset_assets();
  if (configuration_size == 0) {
    configured = 1;
    return MOBAGEN_WASM_STATUS_OK;
  }
  configuration = guest_pointer(configuration_offset, configuration_size);
  if (configuration == NULL || configuration_size < MOBAGEN_WASM_ASSET_CONFIGURATION_V1_SIZE
      || read_u32(configuration) != MOBAGEN_WASM_ASSET_CONFIGURATION_V1_SIZE
      || read_u32(configuration + 4) != MOBAGEN_WASM_ASSET_STORE_V1_PROTOCOL_VERSION || read_u32(configuration + 12) != 0) {
    return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  }
  count = read_u32(configuration + 8);
  if (count > MOBAGEN_WASM_ASSET_MAX_CONFIGURED_ASSETS) {
    return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  }
  cursor = MOBAGEN_WASM_ASSET_CONFIGURATION_V1_SIZE;
  for (index = 0; index < count; ++index) {
    uint32_t payload_size;
    uint32_t record_size;
    uint32_t previous;
    if (cursor > configuration_size || MOBAGEN_WASM_ASSET_CONFIGURATION_ENTRY_V1_SIZE > configuration_size - cursor) {
      return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
    }
    payload_size = read_u32(configuration + cursor + MOBAGEN_WASM_ASSET_ID_V1_SIZE);
    if (read_u32(configuration + cursor + MOBAGEN_WASM_ASSET_ID_V1_SIZE + 4) != 0
        || payload_size > MOBAGEN_WASM_ASSET_MAX_CONFIGURED_BYTES - payload_bytes) {
      return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
    }
    record_size = align_command(MOBAGEN_WASM_ASSET_CONFIGURATION_ENTRY_V1_SIZE + payload_size);
    if (record_size < payload_size || record_size > configuration_size - cursor) {
      return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
    }
    copy_bytes(assets[index].id.bytes, configuration + cursor, MOBAGEN_WASM_ASSET_ID_BYTES);
    for (previous = 0; previous < index; ++previous) {
      if (same_id(&assets[index].id, &assets[previous].id)) {
        return MOBAGEN_WASM_STATUS_CONFLICT;
      }
    }
    copy_bytes(asset_payloads + payload_bytes, configuration + cursor + MOBAGEN_WASM_ASSET_CONFIGURATION_ENTRY_V1_SIZE, payload_size);
    assets[index].payload_offset = payload_bytes;
    assets[index].payload_size = payload_size;
    assets[index].occupied = 1;
    payload_bytes += payload_size;
    cursor += record_size;
  }
  if (cursor != configuration_size) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  asset_count = count;
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
  reset_assets();
  return MOBAGEN_WASM_STATUS_OK;
}

static AssetSlot* find_asset(const MobagenWasmAssetIdV1* id) {
  uint32_t index;
  for (index = 0; index < asset_count; ++index) {
    if (assets[index].occupied && same_id(&assets[index].id, id)) return &assets[index];
  }
  return NULL;
}

static AssetSlot* find_handle(uint32_t index, uint32_t generation) {
  if (index >= asset_count || !assets[index].occupied || assets[index].generation != generation || assets[index].leases == 0) {
    return NULL;
  }
  return &assets[index];
}

static uint32_t write_acquire_result(uint8_t* output, uint32_t capacity, uint32_t request_id, const MobagenWasmAssetIdV1* id) {
  AssetSlot* asset;
  uint32_t index;
  uint32_t status;
  if (capacity < MOBAGEN_WASM_ASSET_ACQUIRE_RESULT_V1_SIZE) return 0;
  asset = find_asset(id);
  status = asset == NULL ? MOBAGEN_WASM_STATUS_NOT_FOUND : MOBAGEN_WASM_STATUS_OK;
  if (asset != NULL && asset->leases == UINT32_MAX) status = MOBAGEN_WASM_STATUS_CONFLICT;
  write_u32(output, MOBAGEN_WASM_ASSET_ACQUIRE_RESULT_V1_SIZE);
  write_u32(output + 4, MOBAGEN_WASM_ASSET_RESULT_ACQUIRE);
  write_u32(output + 8, request_id);
  write_u32(output + 12, status);
  index = status == MOBAGEN_WASM_STATUS_OK ? (uint32_t)(asset - assets) : 0;
  write_u32(output + 16, index);
  write_u32(output + 20, status == MOBAGEN_WASM_STATUS_OK ? asset->generation : 0);
  if (status == MOBAGEN_WASM_STATUS_OK) ++asset->leases;
  return MOBAGEN_WASM_ASSET_ACQUIRE_RESULT_V1_SIZE;
}

static uint32_t write_view_result(uint8_t* output, uint32_t capacity, uint32_t request_id, uint32_t index, uint32_t generation) {
  AssetSlot* asset = find_handle(index, generation);
  uint32_t payload_size = asset == NULL ? 0 : asset->payload_size;
  uint32_t result_size = align_command(MOBAGEN_WASM_ASSET_VIEW_RESULT_V1_SIZE + payload_size);
  if (result_size < payload_size || capacity < result_size) return 0;
  zero_bytes(output, result_size);
  write_u32(output, result_size);
  write_u32(output + 4, MOBAGEN_WASM_ASSET_RESULT_VIEW);
  write_u32(output + 8, request_id);
  write_u32(output + 12, asset == NULL ? MOBAGEN_WASM_STATUS_NOT_FOUND : MOBAGEN_WASM_STATUS_OK);
  write_u32(output + 16, index);
  write_u32(output + 20, generation);
  write_u32(output + 24, payload_size);
  if (asset != NULL) {
    copy_bytes(output + MOBAGEN_WASM_ASSET_VIEW_RESULT_V1_SIZE, asset_payloads + asset->payload_offset, payload_size);
  }
  return result_size;
}

static uint32_t write_release_result(uint8_t* output, uint32_t capacity, uint32_t request_id, uint32_t index, uint32_t generation) {
  AssetSlot* asset;
  if (capacity < MOBAGEN_WASM_ASSET_RELEASE_RESULT_V1_SIZE) return 0;
  asset = find_handle(index, generation);
  write_u32(output, MOBAGEN_WASM_ASSET_RELEASE_RESULT_V1_SIZE);
  write_u32(output + 4, MOBAGEN_WASM_ASSET_RESULT_RELEASE);
  write_u32(output + 8, request_id);
  write_u32(output + 12, asset == NULL ? MOBAGEN_WASM_STATUS_NOT_FOUND : MOBAGEN_WASM_STATUS_OK);
  if (asset != NULL) --asset->leases;
  return MOBAGEN_WASM_ASSET_RELEASE_RESULT_V1_SIZE;
}

static void write_process_result(uint8_t* result, uint32_t status, uint32_t bytes, uint32_t commands) {
  zero_bytes(result, MOBAGEN_WASM_COMMAND_RESULT_V1_SIZE);
  write_u32(result, MOBAGEN_WASM_COMMAND_RESULT_V1_SIZE);
  write_u32(result + 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
  write_u32(result + 8, status);
  write_u32(result + 12, bytes);
  write_u32(result + 16, commands);
}

MOBAGEN_WASM_GUEST_EXPORT uint32_t mobagen_wasm_plugin_process_v1(uint32_t input_batch_offset, uint32_t output_batch_offset, uint32_t result_offset) {
  uint8_t* input_batch = guest_pointer(input_batch_offset, MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE);
  uint8_t* output_batch = guest_pointer(output_batch_offset, MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE);
  uint8_t* result = guest_pointer(result_offset, MOBAGEN_WASM_COMMAND_RESULT_V1_SIZE);
  uint8_t* input;
  uint8_t* output;
  uint32_t input_bytes;
  uint32_t output_capacity;
  uint32_t command_count;
  uint32_t input_cursor = 0;
  uint32_t output_cursor = 0;
  uint32_t command;
  if (!started || input_batch == NULL || output_batch == NULL || result == NULL) {
    return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  }
  if (read_u32(input_batch) != MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE || read_u32(input_batch + 4) != MOBAGEN_WASM_PLUGIN_ABI_VERSION
      || read_u32(output_batch) != MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE || read_u32(output_batch + 4) != MOBAGEN_WASM_PLUGIN_ABI_VERSION) {
    return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  }
  input_bytes = read_u32(input_batch + 12);
  command_count = read_u32(input_batch + 16);
  output_capacity = read_u32(output_batch + 12);
  input = guest_pointer(read_u32(input_batch + 8), input_bytes);
  output = guest_pointer(read_u32(output_batch + 8), output_capacity);
  if (input == NULL || output == NULL || command_count > MOBAGEN_WASM_MAX_COMMANDS_PER_BATCH) {
    return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  }

  for (command = 0; command < command_count; ++command) {
    uint32_t byte_size;
    uint32_t opcode;
    uint32_t request_id;
    uint32_t written = 0;
    if (input_cursor > input_bytes || 8U > input_bytes - input_cursor) {
      return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
    }
    byte_size = read_u32(input + input_cursor);
    opcode = read_u32(input + input_cursor + 4);
    if (byte_size < 8U || byte_size % MOBAGEN_WASM_COMMAND_ALIGNMENT != 0 || byte_size > input_bytes - input_cursor) {
      return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
    }
    if (byte_size < 16U) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
    request_id = read_u32(input + input_cursor + 8);
    if (opcode == MOBAGEN_WASM_ASSET_COMMAND_ACQUIRE && byte_size == MOBAGEN_WASM_ASSET_ACQUIRE_COMMAND_V1_SIZE
        && read_u32(input + input_cursor + 12) == 0) {
      written = write_acquire_result(output + output_cursor, output_capacity - output_cursor, request_id,
                                     (const MobagenWasmAssetIdV1*)(input + input_cursor + 16));
    } else if (opcode == MOBAGEN_WASM_ASSET_COMMAND_VIEW && byte_size == MOBAGEN_WASM_ASSET_HANDLE_COMMAND_V1_SIZE
               && read_u32(input + input_cursor + 12) == 0) {
      written = write_view_result(output + output_cursor, output_capacity - output_cursor, request_id, read_u32(input + input_cursor + 16),
                                  read_u32(input + input_cursor + 20));
    } else if (opcode == MOBAGEN_WASM_ASSET_COMMAND_RELEASE && byte_size == MOBAGEN_WASM_ASSET_HANDLE_COMMAND_V1_SIZE
               && read_u32(input + input_cursor + 12) == 0) {
      written = write_release_result(output + output_cursor, output_capacity - output_cursor, request_id, read_u32(input + input_cursor + 16),
                                     read_u32(input + input_cursor + 20));
    } else {
      return MOBAGEN_WASM_STATUS_UNSUPPORTED;
    }
    if (written == 0) {
      write_u32(output_batch + 12, 0);
      write_u32(output_batch + 16, 0);
      write_process_result(result, MOBAGEN_WASM_STATUS_OUT_OF_MEMORY, 0, 0);
      return MOBAGEN_WASM_STATUS_OK;
    }
    input_cursor += byte_size;
    output_cursor += written;
  }
  if (input_cursor != input_bytes) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
  write_u32(output_batch + 12, output_cursor);
  write_u32(output_batch + 16, command_count);
  write_process_result(result, MOBAGEN_WASM_STATUS_OK, output_cursor, command_count);
  return MOBAGEN_WASM_STATUS_OK;
}

#if defined(MOBAGEN_WASM_ASSET_STORE_TESTING)
uint8_t* mobagen_wasm_asset_store_test_memory_v1(void) {
  ensure_metadata();
  return linear_memory;
}

uint32_t mobagen_wasm_asset_store_test_memory_size_v1(void) { return LINEAR_MEMORY_BYTES; }

void mobagen_wasm_asset_store_test_reset_v1(void) {
  zero_bytes(linear_memory, LINEAR_MEMORY_BYTES);
  zero_bytes(asset_payloads, MOBAGEN_WASM_ASSET_MAX_CONFIGURED_BYTES);
  zero_bytes((uint8_t*)allocations, (uint32_t)sizeof(allocations));
  zero_bytes((uint8_t*)assets, (uint32_t)sizeof(assets));
  asset_count = 0;
  payload_bytes = 0;
  metadata_ready = 0;
  configured = 0;
  started = 0;
}
#endif
