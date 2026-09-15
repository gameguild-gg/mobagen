#pragma once

#include "asset_cache.hpp"
#include "asset_dependency_graph.hpp"
#include "asset_registry.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

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

  enum class AssetManagerBatchStatus : std::uint8_t {
    success,
    dependency_error,
    asset_error,
  };

  struct AssetManagerResolvedAsset {
    AssetId id;
    resource::Handle handle{resource::kNullHandle};
  };

  struct AssetManagerBatchResult {
    AssetManagerBatchStatus status{AssetManagerBatchStatus::dependency_error};
    AssetDependencyStatus dependency_status{AssetDependencyStatus::success};
    std::optional<AssetId> failed_asset;
    std::optional<AssetManagerAcquireResult> failure;
    std::vector<AssetManagerResolvedAsset> assets;

    [[nodiscard]] bool ok() const noexcept {
      return status == AssetManagerBatchStatus::success;
    }
  };

  /* Lazily materializes content-addressed cache blobs into typed, generational
     runtime handles. The cache and decoder context must outlive the manager.
     Every successful acquire owns one lease that must be released. Callers
     provide synchronization; resident acquire/release and get(handle) are
     allocation-free after warm-up. */
  template <class T> class AssetManager {
    static_assert(std::is_default_constructible_v<T>);

  public:
    AssetManager(const AssetCache& cache, AssetDecoder<T> decoder) noexcept
        : cache_(&cache), decoder_(decoder) {}

    [[nodiscard]] AssetManagerAcquireResult acquire(const AssetId& id) {
      if (const auto resident = registry_.find(id); resident.has_value()) {
        if (!retain(*resident)) {
          return {.status = AssetManagerStatus::registry_error};
        }
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
        if (!inserted.inserted) {
          if (!retain(inserted.handle)) {
            return {.status = AssetManagerStatus::registry_error};
          }
          return {
              .status = AssetManagerStatus::resident,
              .handle = inserted.handle,
              .cache_status = cached.status,
          };
        }

        try {
          if (inserted.handle.index >= reference_counts_.size()) {
            reference_counts_.resize(
                static_cast<std::size_t>(inserted.handle.index) + 1
            );
          }
          if (reference_counts_[inserted.handle.index] != 0) {
            (void)registry_.release(inserted.handle);
            return {
                .status = AssetManagerStatus::registry_error,
                .cache_status = cached.status,
            };
          }
          reference_counts_[inserted.handle.index] = 1;
        } catch (...) {
          (void)registry_.release(inserted.handle);
          throw;
        }
        return {
            .status = AssetManagerStatus::loaded,
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

    [[nodiscard]] AssetManagerBatchResult acquire_all(
        const AssetDependencyGraph& graph, std::span<const AssetId> roots
    ) {
      auto order = graph.build_order(roots);
      if (order.status != AssetDependencyStatus::success) {
        return {
            .status = AssetManagerBatchStatus::dependency_error,
            .dependency_status = order.status,
        };
      }

      std::vector<AssetManagerResolvedAsset> resolved;
      std::vector<resource::Handle> acquired_handles;
      resolved.reserve(order.assets.size());
      acquired_handles.reserve(order.assets.size());

      for (const auto& id : order.assets) {
        const auto acquired = acquire(id);
        if (!acquired.ok()) {
          for (auto handle = acquired_handles.rbegin();
               handle != acquired_handles.rend(); ++handle) {
            (void)release(*handle);
          }
          return {
              .status = AssetManagerBatchStatus::asset_error,
              .dependency_status = AssetDependencyStatus::success,
              .failed_asset = id,
              .failure = acquired,
          };
        }
        acquired_handles.push_back(acquired.handle);
        resolved.push_back({id, acquired.handle});
      }

      return {
          .status = AssetManagerBatchStatus::success,
          .dependency_status = AssetDependencyStatus::success,
          .assets = std::move(resolved),
      };
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
      if (!registry_.valid(handle)
          || handle.index >= reference_counts_.size()) {
        return false;
      }
      auto& references = reference_counts_[handle.index];
      if (references == 0) {
        return false;
      }
      if (references > 1) {
        --references;
        return true;
      }
      if (!registry_.release(handle)) {
        return false;
      }
      references = 0;
      return true;
    }
    [[nodiscard]] std::size_t size() const noexcept {
      return registry_.size();
    }

  private:
    [[nodiscard]] bool retain(resource::Handle handle) noexcept {
      if (!registry_.valid(handle)
          || handle.index >= reference_counts_.size()) {
        return false;
      }
      auto& references = reference_counts_[handle.index];
      if (references == 0
          || references == std::numeric_limits<std::uint32_t>::max()) {
        return false;
      }
      ++references;
      return true;
    }

    const AssetCache* cache_{};
    AssetDecoder<T> decoder_;
    AssetRegistry<T> registry_;
    std::vector<std::uint32_t> reference_counts_;
  };

}  // namespace mobagen::assets
