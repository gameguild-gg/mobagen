#include <mobagen/plugin/runtime_tick_v1.h>
#include <mobagen/plugin/wasm_abi.h>
#include <mobagen/version.h>

#if !defined(MOBAGEN_SDK_VERSION_MAJOR) || !defined(MOBAGEN_SDK_VERSION_MINOR) || !defined(MOBAGEN_SDK_VERSION_PATCH) \
    || !defined(MOBAGEN_SDK_VERSION_STRING)
#  error "Mobagen SDK version contract is unavailable"
#endif

MOBAGEN_PLUGIN_EXPORT MobagenStatus MOBAGEN_PLUGIN_CALL mobagen_plugin_entry_v1(const MobagenHostApiV1* host, MobagenPluginDescriptorV1* descriptor) {
  MobagenRuntimeTickV1 compile_contract = {0};
  MobagenWasmCommandBatchV1 wasm_batch = {0};
  compile_contract.header.struct_size = MOBAGEN_RUNTIME_TICK_V1_SIZE;
  wasm_batch.struct_size = MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE;
  (void)host;
  (void)descriptor;
  return compile_contract.header.struct_size == sizeof(compile_contract) && wasm_batch.struct_size == sizeof(wasm_batch)
                 && MOBAGEN_SDK_VERSION_STRING[0] != '\0'
             ? MOBAGEN_STATUS_UNSUPPORTED
             : MOBAGEN_STATUS_FAILED;
}
