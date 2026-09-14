#include "plugins/plugin_abi.h"

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

int mobagen_plugin_abi_c_compile_test(void) {
  MobagenHostApiV1 host = {0};
  MobagenPluginDescriptorV1 descriptor = {0};
  host.struct_size = (uint32_t)sizeof(host);
  host.abi_version = MOBAGEN_PLUGIN_ABI_VERSION;
  host.allocate = allocate_memory;
  host.deallocate = release_memory;
  descriptor.struct_size = (uint32_t)sizeof(descriptor);
  descriptor.abi_version = MOBAGEN_PLUGIN_ABI_VERSION;
  descriptor.lifecycle.struct_size = (uint32_t)sizeof(descriptor.lifecycle);
  descriptor.lifecycle.configure = configure_plugin;
  return descriptor.lifecycle.configure(NULL, &host, (MobagenByteView){NULL, 0}) == MOBAGEN_STATUS_OK ? 0 : 1;
}
