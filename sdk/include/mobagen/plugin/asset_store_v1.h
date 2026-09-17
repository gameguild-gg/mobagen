#ifndef MOBAGEN_ASSET_STORE_V1_H
#define MOBAGEN_ASSET_STORE_V1_H

#include "plugin_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOBAGEN_ASSET_STORE_V1_ID "assets.store.v1"
#define MOBAGEN_ASSET_STORE_V1_ABI_VERSION UINT32_C(1)

/* Binary SHA-256 identity avoids parsing and allocation at the capability seam. */
typedef struct MobagenAssetIdV1 {
  uint8_t bytes[32];
} MobagenAssetIdV1;

typedef struct MobagenAssetHandleV1 {
  uint32_t index;
  uint32_t generation;
} MobagenAssetHandleV1;

/*
 * acquire lazily makes immutable content resident and creates one lease.
 * view performs no allocation and its byte span remains valid through the
 * caller's matching release. Stale handles return MOBAGEN_STATUS_NOT_FOUND.
 * Providers synchronize these callbacks for concurrent host threads.
 */
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenAssetAcquireFnV1)(void* store_state, const MobagenAssetIdV1* id, MobagenAssetHandleV1* handle);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenAssetViewFnV1)(void* store_state, MobagenAssetHandleV1 handle, MobagenByteView* bytes);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenAssetReleaseFnV1)(void* store_state, MobagenAssetHandleV1 handle);

typedef struct MobagenAssetStoreV1 {
  MobagenCapabilityHeaderV1 header;
  void* store_state;
  MobagenAssetAcquireFnV1 acquire;
  MobagenAssetViewFnV1 view;
  MobagenAssetReleaseFnV1 release;
} MobagenAssetStoreV1;

#define MOBAGEN_ASSET_ID_V1_SIZE ((uint32_t)sizeof(MobagenAssetIdV1))
#define MOBAGEN_ASSET_HANDLE_V1_SIZE ((uint32_t)sizeof(MobagenAssetHandleV1))
#define MOBAGEN_ASSET_STORE_V1_SIZE ((uint32_t)sizeof(MobagenAssetStoreV1))

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MOBAGEN_ASSET_STORE_V1_H */
