#include <mobagen/plugin/runtime_tick_v1.h>

MOBAGEN_PLUGIN_EXPORT MobagenStatus MOBAGEN_PLUGIN_CALL mobagen_plugin_entry_v1(const MobagenHostApiV1* host, MobagenPluginDescriptorV1* descriptor) {
  MobagenRuntimeTickV1 compile_contract = {0};
  compile_contract.header.struct_size = MOBAGEN_RUNTIME_TICK_V1_SIZE;
  (void)host;
  (void)descriptor;
  return compile_contract.header.struct_size == sizeof(compile_contract) ? MOBAGEN_STATUS_UNSUPPORTED : MOBAGEN_STATUS_FAILED;
}
