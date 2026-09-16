#ifndef MOBAGEN_PLUGIN_WASM_ASSET_STORE_V1_H
#define MOBAGEN_PLUGIN_WASM_ASSET_STORE_V1_H

#include "wasm_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOBAGEN_WASM_ASSET_STORE_V1_ID "assets.store.v1"
#define MOBAGEN_WASM_ASSET_STORE_V1_PROTOCOL_VERSION UINT32_C(1)
#define MOBAGEN_WASM_ASSET_ID_BYTES UINT32_C(32)
#define MOBAGEN_WASM_ASSET_MAX_CONFIGURED_ASSETS UINT32_C(16)
#define MOBAGEN_WASM_ASSET_MAX_CONFIGURED_BYTES UINT32_C(262144)

#define MOBAGEN_WASM_ASSET_COMMAND_ACQUIRE UINT32_C(0x00010001)
#define MOBAGEN_WASM_ASSET_COMMAND_VIEW UINT32_C(0x00010002)
#define MOBAGEN_WASM_ASSET_COMMAND_RELEASE UINT32_C(0x00010003)
#define MOBAGEN_WASM_ASSET_RESULT_ACQUIRE UINT32_C(0x80010001)
#define MOBAGEN_WASM_ASSET_RESULT_VIEW UINT32_C(0x80010002)
#define MOBAGEN_WASM_ASSET_RESULT_RELEASE UINT32_C(0x80010003)

typedef struct MobagenWasmAssetIdV1 {
  uint8_t bytes[32];
} MobagenWasmAssetIdV1;

/*
 * Portable provider configuration. Entries immediately follow the header.
 * Each entry prefix is followed by payload_size bytes and then zero padding to
 * MOBAGEN_WASM_COMMAND_ALIGNMENT. The host verifies content identities before
 * passing this immutable seed data across the sandbox boundary.
 */
typedef struct MobagenWasmAssetConfigurationV1 {
  uint32_t struct_size;
  uint32_t protocol_version;
  uint32_t asset_count;
  uint32_t reserved;
} MobagenWasmAssetConfigurationV1;

typedef struct MobagenWasmAssetConfigurationEntryV1 {
  MobagenWasmAssetIdV1 id;
  uint32_t payload_size;
  uint32_t reserved;
} MobagenWasmAssetConfigurationEntryV1;

typedef struct MobagenWasmAssetAcquireCommandV1 {
  MobagenWasmCommandHeaderV1 header;
  uint32_t request_id;
  uint32_t reserved;
  MobagenWasmAssetIdV1 id;
} MobagenWasmAssetAcquireCommandV1;

typedef struct MobagenWasmAssetHandleCommandV1 {
  MobagenWasmCommandHeaderV1 header;
  uint32_t request_id;
  uint32_t reserved;
  MobagenWasmHandle32 handle;
} MobagenWasmAssetHandleCommandV1;

typedef struct MobagenWasmAssetAcquireResultV1 {
  MobagenWasmCommandHeaderV1 header;
  uint32_t request_id;
  uint32_t status;
  MobagenWasmHandle32 handle;
} MobagenWasmAssetAcquireResultV1;

/* payload_size bytes immediately follow this prefix; byte_size includes padding. */
typedef struct MobagenWasmAssetViewResultV1 {
  MobagenWasmCommandHeaderV1 header;
  uint32_t request_id;
  uint32_t status;
  MobagenWasmHandle32 handle;
  uint32_t payload_size;
  uint32_t reserved;
} MobagenWasmAssetViewResultV1;

typedef struct MobagenWasmAssetReleaseResultV1 {
  MobagenWasmCommandHeaderV1 header;
  uint32_t request_id;
  uint32_t status;
} MobagenWasmAssetReleaseResultV1;

#define MOBAGEN_WASM_ASSET_ID_V1_SIZE ((uint32_t)sizeof(MobagenWasmAssetIdV1))
#define MOBAGEN_WASM_ASSET_CONFIGURATION_V1_SIZE ((uint32_t)sizeof(MobagenWasmAssetConfigurationV1))
#define MOBAGEN_WASM_ASSET_CONFIGURATION_ENTRY_V1_SIZE ((uint32_t)sizeof(MobagenWasmAssetConfigurationEntryV1))
#define MOBAGEN_WASM_ASSET_ACQUIRE_COMMAND_V1_SIZE ((uint32_t)sizeof(MobagenWasmAssetAcquireCommandV1))
#define MOBAGEN_WASM_ASSET_HANDLE_COMMAND_V1_SIZE ((uint32_t)sizeof(MobagenWasmAssetHandleCommandV1))
#define MOBAGEN_WASM_ASSET_ACQUIRE_RESULT_V1_SIZE ((uint32_t)sizeof(MobagenWasmAssetAcquireResultV1))
#define MOBAGEN_WASM_ASSET_VIEW_RESULT_V1_SIZE ((uint32_t)sizeof(MobagenWasmAssetViewResultV1))
#define MOBAGEN_WASM_ASSET_RELEASE_RESULT_V1_SIZE ((uint32_t)sizeof(MobagenWasmAssetReleaseResultV1))

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MOBAGEN_PLUGIN_WASM_ASSET_STORE_V1_H */
