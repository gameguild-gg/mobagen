#pragma once

#include "asset_manager.hpp"

#include <mobagen/plugin/asset_store_v1.h>

#include <cstddef>
#include <mutex>
#include <vector>

namespace mobagen::assets {

  /* Default native provider for assets.store.v1. The service is deliberately a
     separate library so applications can replace it without carrying this
     implementation in the module-manager-only host. The cache must outlive the
     service and all outstanding asset leases. */
  class NativeAssetStoreService {
  public:
    explicit NativeAssetStoreService(const AssetCache& cache) noexcept;
    NativeAssetStoreService(const NativeAssetStoreService&) = delete;
    NativeAssetStoreService& operator=(const NativeAssetStoreService&) = delete;
    NativeAssetStoreService(NativeAssetStoreService&&) = delete;
    NativeAssetStoreService& operator=(NativeAssetStoreService&&) = delete;

    [[nodiscard]] const MobagenAssetStoreV1& api() const noexcept { return api_; }

  private:
    using Blob = std::vector<std::byte>;

    static bool decode(void* context, const AssetDecodeRequest& request, Blob& output);
    static MobagenStatus MOBAGEN_PLUGIN_CALL acquire(void* store_state, const MobagenAssetIdV1* id, MobagenAssetHandleV1* handle) noexcept;
    static MobagenStatus MOBAGEN_PLUGIN_CALL view(void* store_state, MobagenAssetHandleV1 handle, MobagenByteView* bytes) noexcept;
    static MobagenStatus MOBAGEN_PLUGIN_CALL release(void* store_state, MobagenAssetHandleV1 handle) noexcept;

    std::mutex mutex_;
    AssetManager<Blob> manager_;
    MobagenAssetStoreV1 api_{};
  };

}  // namespace mobagen::assets
