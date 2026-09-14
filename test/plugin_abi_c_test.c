#include "plugins/plugin_abi.h"
#include "plugins/runtime_tick_v1.h"
#include <mobagen/plugin/wasm_abi.h>

#include <stddef.h>
#include <stdint.h>

static void* MOBAGEN_PLUGIN_CALL allocate_memory(void* host_context, size_t size, size_t alignment) {
  (void)host_context;
  (void)size;
  (void)alignment;
  return NULL;
}

static void MOBAGEN_PLUGIN_CALL release_memory(void* host_context, void* memory, size_t size, size_t alignment) {
  (void)host_context;
  (void)memory;
  (void)size;
  (void)alignment;
}

static MobagenStatus MOBAGEN_PLUGIN_CALL configure_plugin(void* plugin_state, const MobagenHostApiV1* host, MobagenByteView configuration) {
  (void)plugin_state;
  (void)configuration;
  return host != NULL && host->abi_version == MOBAGEN_PLUGIN_ABI_VERSION ? MOBAGEN_STATUS_OK : MOBAGEN_STATUS_INVALID_ARGUMENT;
}

static uint32_t wasm_allocate(uint32_t size, uint32_t alignment) { return size == 8 && alignment == MOBAGEN_WASM_EXCHANGE_ALIGNMENT ? 8 : 0; }

static uint32_t wasm_deallocate(uint32_t offset, uint32_t size, uint32_t alignment) {
  return offset == 8 && size == 8 && alignment == MOBAGEN_WASM_EXCHANGE_ALIGNMENT ? MOBAGEN_WASM_STATUS_OK : MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
}

int mobagen_plugin_abi_c_compile_test(void) {
  MobagenHostApiV1 host = {0};
  MobagenPluginDescriptorV1 descriptor = {0};
  MobagenRuntimeTickV1 tick = {0};
  host.struct_size = (uint32_t)sizeof(host);
  host.abi_version = MOBAGEN_PLUGIN_ABI_VERSION;
  host.allocate = allocate_memory;
  host.deallocate = release_memory;
  descriptor.struct_size = (uint32_t)sizeof(descriptor);
  descriptor.abi_version = MOBAGEN_PLUGIN_ABI_VERSION;
  descriptor.lifecycle.struct_size = (uint32_t)sizeof(descriptor.lifecycle);
  descriptor.lifecycle.configure = configure_plugin;
  tick.header.struct_size = MOBAGEN_RUNTIME_TICK_V1_SIZE;
  tick.header.abi_version = 1;
  return descriptor.lifecycle.configure(NULL, &host, (MobagenByteView){NULL, 0}) == MOBAGEN_STATUS_OK && tick.header.struct_size == sizeof(tick) ? 0
                                                                                                                                                 : 1;
}

int mobagen_wasm_abi_c_compile_test(void) {
  MobagenWasmPluginDescriptorV1 descriptor = {0};
  MobagenWasmCommandBatchV1 batch = {0};
  MobagenWasmPluginAllocateV1Fn allocate = wasm_allocate;
  MobagenWasmPluginDeallocateV1Fn deallocate = wasm_deallocate;
  descriptor.struct_size = MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE;
  descriptor.abi_version = MOBAGEN_WASM_PLUGIN_ABI_VERSION;
  batch.struct_size = MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE;
  batch.abi_version = MOBAGEN_WASM_PLUGIN_ABI_VERSION;
  return descriptor.struct_size == sizeof(descriptor) && batch.struct_size == sizeof(batch) && allocate(8, MOBAGEN_WASM_EXCHANGE_ALIGNMENT) == 8
                 && deallocate(8, 8, MOBAGEN_WASM_EXCHANGE_ALIGNMENT) == MOBAGEN_WASM_STATUS_OK
             ? 0
             : 1;
}
