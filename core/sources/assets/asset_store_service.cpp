#include "asset_store_service.hpp"

#include <algorithm>
#include <new>

namespace mobagen::assets {
  namespace {

    [[nodiscard]] AssetId internal_id(const MobagenAssetIdV1& id) noexcept {
      AssetId result;
      std::ranges::copy(id.bytes, result.bytes.begin());
      return result;
    }

    [[nodiscard]] resource::Handle internal_handle(
        MobagenAssetHandleV1 handle
    ) noexcept {
      return {handle.index, handle.generation};
    }

    [[nodiscard]] MobagenStatus acquire_status(AssetManagerStatus status) noexcept {
      switch (status) {
        case AssetManagerStatus::resident:
        case AssetManagerStatus::loaded:
          return MOBAGEN_STATUS_OK;
        case AssetManagerStatus::not_found:
          return MOBAGEN_STATUS_NOT_FOUND;
        case AssetManagerStatus::invalid_decoder:
        case AssetManagerStatus::cache_error:
        case AssetManagerStatus::decode_failed:
        case AssetManagerStatus::registry_error:
          return MOBAGEN_STATUS_FAILED;
      }
      return MOBAGEN_STATUS_FAILED;
    }

  }  // namespace

  NativeAssetStoreService::NativeAssetStoreService(const AssetCache& cache) noexcept
      : manager_(cache, {.context = nullptr, .decode = decode}),
        api_{
            .header = {MOBAGEN_ASSET_STORE_V1_SIZE,
                       MOBAGEN_ASSET_STORE_V1_ABI_VERSION},
            .store_state = this,
            .acquire = acquire,
            .view = view,
            .release = release,
        } {}

  bool NativeAssetStoreService::decode(
      void*, const AssetDecodeRequest& request, Blob& output
  ) {
    output.assign(request.bytes.begin(), request.bytes.end());
    return true;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL NativeAssetStoreService::acquire(
      void* store_state, const MobagenAssetIdV1* id,
      MobagenAssetHandleV1* handle
  ) noexcept {
    if (store_state == nullptr || id == nullptr || handle == nullptr) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    auto* service = static_cast<NativeAssetStoreService*>(store_state);
    try {
      const std::lock_guard lock{service->mutex_};
      const auto acquired = service->manager_.acquire(internal_id(*id));
      const auto status = acquire_status(acquired.status);
      if (status == MOBAGEN_STATUS_OK) {
        *handle = {acquired.handle.index, acquired.handle.generation};
      }
      return status;
    } catch (const std::bad_alloc&) {
      return MOBAGEN_STATUS_OUT_OF_MEMORY;
    } catch (...) {
      return MOBAGEN_STATUS_FAILED;
    }
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL NativeAssetStoreService::view(
      void* store_state, MobagenAssetHandleV1 handle, MobagenByteView* bytes
  ) noexcept {
    if (store_state == nullptr || bytes == nullptr) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    auto* service = static_cast<NativeAssetStoreService*>(store_state);
    try {
      const std::lock_guard lock{service->mutex_};
      const auto* blob = service->manager_.get(internal_handle(handle));
      if (blob == nullptr) {
        return MOBAGEN_STATUS_NOT_FOUND;
      }
      *bytes = {
          reinterpret_cast<const std::uint8_t*>(blob->data()), blob->size()
      };
      return MOBAGEN_STATUS_OK;
    } catch (...) {
      return MOBAGEN_STATUS_FAILED;
    }
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL NativeAssetStoreService::release(
      void* store_state, MobagenAssetHandleV1 handle
  ) noexcept {
    if (store_state == nullptr) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    auto* service = static_cast<NativeAssetStoreService*>(store_state);
    try {
      const std::lock_guard lock{service->mutex_};
      return service->manager_.release(internal_handle(handle))
                 ? MOBAGEN_STATUS_OK
                 : MOBAGEN_STATUS_NOT_FOUND;
    } catch (...) {
      return MOBAGEN_STATUS_FAILED;
    }
  }

}  // namespace mobagen::assets
