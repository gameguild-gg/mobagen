#include "plugins/plugin_abi.h"
#include "plugins/runtime_tick_v1.h"
#include <mobagen/plugin/asset_store_v1.h>
#include <mobagen/plugin/render_backend_v1.h>
#include <mobagen/plugin/wasm_abi.h>
#include <mobagen/plugin/wasm_asset_store_v1.h>
#include <mobagen/plugin/window_surface_v1.h>

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

static MobagenStatus MOBAGEN_PLUGIN_CALL acquire_asset(void* store_state, const MobagenAssetIdV1* id, MobagenAssetHandleV1* handle) {
  if (store_state == NULL || id == NULL || handle == NULL) return MOBAGEN_STATUS_INVALID_ARGUMENT;
  handle->index = id->bytes[0];
  handle->generation = 7;
  return MOBAGEN_STATUS_OK;
}

static MobagenStatus MOBAGEN_PLUGIN_CALL view_asset(void* store_state, MobagenAssetHandleV1 handle, MobagenByteView* bytes) {
  static const uint8_t payload[] = {1, 2, 3};
  if (store_state == NULL || bytes == NULL || handle.generation != 7) return MOBAGEN_STATUS_INVALID_ARGUMENT;
  bytes->data = payload;
  bytes->size = sizeof(payload);
  return MOBAGEN_STATUS_OK;
}

static MobagenStatus MOBAGEN_PLUGIN_CALL release_asset(void* store_state, MobagenAssetHandleV1 handle) {
  return store_state != NULL && handle.generation == 7 ? MOBAGEN_STATUS_OK : MOBAGEN_STATUS_INVALID_ARGUMENT;
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

int mobagen_wasm_asset_store_abi_c_compile_test(void) {
  MobagenWasmAssetAcquireCommandV1 acquire = {0};
  MobagenWasmAssetHandleCommandV1 view = {0};
  MobagenWasmAssetViewResultV1 result = {0};
  acquire.header.byte_size = MOBAGEN_WASM_ASSET_ACQUIRE_COMMAND_V1_SIZE;
  acquire.header.opcode = MOBAGEN_WASM_ASSET_COMMAND_ACQUIRE;
  view.header.byte_size = MOBAGEN_WASM_ASSET_HANDLE_COMMAND_V1_SIZE;
  view.header.opcode = MOBAGEN_WASM_ASSET_COMMAND_VIEW;
  result.header.byte_size = MOBAGEN_WASM_ASSET_VIEW_RESULT_V1_SIZE;
  result.header.opcode = MOBAGEN_WASM_ASSET_RESULT_VIEW;
  return acquire.header.byte_size == sizeof(acquire)
                 && view.header.byte_size == sizeof(view)
                 && result.header.byte_size == sizeof(result)
                 && sizeof(MobagenWasmAssetConfigurationV1) == 16
                 && sizeof(MobagenWasmAssetConfigurationEntryV1) == 40
             ? 0
             : 1;
}

int mobagen_asset_store_abi_c_compile_test(void) {
  uint8_t state = 1;
  MobagenAssetIdV1 id = {{5}};
  MobagenAssetHandleV1 handle = {0};
  MobagenByteView bytes = {0};
  MobagenAssetStoreV1 store = {0};
  store.header.struct_size = MOBAGEN_ASSET_STORE_V1_SIZE;
  store.header.abi_version = MOBAGEN_ASSET_STORE_V1_ABI_VERSION;
  store.store_state = &state;
  store.acquire = acquire_asset;
  store.view = view_asset;
  store.release = release_asset;

  return store.header.struct_size == sizeof(store)
                 && store.acquire(store.store_state, &id, &handle) == MOBAGEN_STATUS_OK
                 && handle.index == 5 && handle.generation == 7
                 && store.view(store.store_state, handle, &bytes) == MOBAGEN_STATUS_OK
                 && bytes.size == 3 && bytes.data[2] == 3
                 && store.release(store.store_state, handle) == MOBAGEN_STATUS_OK
             ? 0
             : 1;
}

int mobagen_runtime_adapter_abi_c_compile_test(void) {
  MobagenWindowSurfaceV1 windows = {0};
  MobagenRenderBackendV1 renderer = {0};
  MobagenNativeSurfaceV1 surface = {0};
  MobagenRenderContextDescV1 context = {0};
  windows.header.struct_size = MOBAGEN_WINDOW_SURFACE_V1_SIZE;
  windows.header.abi_version = MOBAGEN_WINDOW_SURFACE_V1_ABI_VERSION;
  renderer.header.struct_size = MOBAGEN_RENDER_BACKEND_V1_SIZE;
  renderer.header.abi_version = MOBAGEN_RENDER_BACKEND_V1_ABI_VERSION;
  surface.struct_size = MOBAGEN_NATIVE_SURFACE_V1_SIZE;
  context.struct_size = MOBAGEN_RENDER_CONTEXT_DESC_V1_SIZE;
  return windows.header.struct_size == sizeof(windows)
                 && renderer.header.struct_size == sizeof(renderer)
                 && surface.struct_size == sizeof(surface)
                 && context.struct_size == sizeof(context)
             ? 0
             : 1;
}
