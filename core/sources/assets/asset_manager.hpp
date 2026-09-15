#pragma once

#include "asset_cache.hpp"
#include "asset_registry.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace mobagen::assets {

  struct AssetDecodeRequest {
    AssetId id;
    std::span<const std::byte> bytes;
  };

  template <class T> struct AssetDecoder {
    void* context{};
    bool (*decode)(void* context, const AssetDecodeRequest& request, T& output){};
  };

  enum class AssetManagerStatus : std::uint8_t {
    resident,
    loaded,
    not_found,
    invalid_decoder,
    cache_error,
    decode_failed,
    registry_error,
  };

  struct AssetManagerAcquireResult {
    AssetManagerStatus status{AssetManagerStatus::cache_error};
    resource::Handle handle{resource::kNullHandle};
    std::optional<AssetCacheStatus> cache_status;

    [[nodiscard]] bool ok() const noexcept {
      return status == AssetManagerStatus::resident
             || status == AssetManagerStatus::loaded;
    }
  };

  /* Lazily materializes content-addressed cache blobs into typed, generational
     runtime handles. The cache and decoder context must outlive the manager.
     Callers provide synchronization around acquire/release; get(handle) is the
     allocation-free hot path after acquisition. */
  template <class T> class AssetManager {
    static_assert(std::is_default_constructible_v<T>);

  public:
    AssetManager(const AssetCache& cache, AssetDecoder<T> decoder) noexcept
        : cache_(&cache), decoder_(decoder) {}

    [[nodiscard]] AssetManagerAcquireResult acquire(const AssetId& id) {
      if (const auto resident = registry_.find(id); resident.has_value()) {
        return {
            .status = AssetManagerStatus::resident,
            .handle = *resident,
        };
      }
      if (decoder_.decode == nullptr) {
        return {.status = AssetManagerStatus::invalid_decoder};
      }

      auto cached = cache_->load(id);
      if (!cached.ok()) {
        return {
            .status = cached.status == AssetCacheStatus::not_found
                        ? AssetManagerStatus::not_found
                        : AssetManagerStatus::cache_error,
            .cache_status = cached.status,
        };
      }

      std::optional<T> decoded;
      try {
        decoded.emplace();
        if (!decoder_.decode(
                decoder_.context, AssetDecodeRequest{id, cached.bytes}, *decoded
            )) {
          return {
              .status = AssetManagerStatus::decode_failed,
              .cache_status = cached.status,
          };
        }
      } catch (...) {
        return {
            .status = AssetManagerStatus::decode_failed,
            .cache_status = cached.status,
        };
      }

      try {
        const auto inserted = registry_.emplace(id, std::move(*decoded));
        return {
            .status = inserted.inserted ? AssetManagerStatus::loaded
                                        : AssetManagerStatus::resident,
            .handle = inserted.handle,
            .cache_status = cached.status,
        };
      } catch (...) {
        return {
            .status = AssetManagerStatus::registry_error,
            .cache_status = cached.status,
        };
      }
    }

    [[nodiscard]] std::optional<resource::Handle> find(const AssetId& id) const {
      return registry_.find(id);
    }
    [[nodiscard]] bool valid(resource::Handle handle) const {
      return registry_.valid(handle);
    }
    [[nodiscard]] T* get(resource::Handle handle) {
      return registry_.get(handle);
    }
    [[nodiscard]] const T* get(resource::Handle handle) const {
      return registry_.get(handle);
    }
    bool release(resource::Handle handle) {
      return registry_.release(handle);
    }
    [[nodiscard]] std::size_t size() const noexcept {
      return registry_.size();
    }

  private:
    const AssetCache* cache_{};
    AssetDecoder<T> decoder_;
    AssetRegistry<T> registry_;
  };

}  // namespace mobagen::assets
