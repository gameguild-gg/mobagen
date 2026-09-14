#pragma once

#include "asset_id.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace mobagen::assets {

  inline constexpr std::size_t default_asset_cache_blob_limit = 1024ULL * 1024ULL * 1024ULL;

  enum class AssetCacheStatus : std::uint8_t {
    stored,
    already_present,
    loaded,
    not_found,
    invalid_root,
    too_large,
    integrity_error,
    io_error,
  };

  struct AssetCacheStoreResult {
    AssetCacheStatus status{AssetCacheStatus::io_error};
    std::optional<AssetId> id;
    std::filesystem::path path;
    std::error_code system_error;

    [[nodiscard]] bool ok() const noexcept { return status == AssetCacheStatus::stored || status == AssetCacheStatus::already_present; }
  };

  struct AssetCacheLoadResult {
    AssetCacheStatus status{AssetCacheStatus::io_error};
    std::vector<std::byte> bytes;
    std::error_code system_error;

    [[nodiscard]] bool ok() const noexcept { return status == AssetCacheStatus::loaded; }
  };

  class AssetCache {
  public:
    explicit AssetCache(std::filesystem::path root, std::size_t max_blob_bytes = default_asset_cache_blob_limit)
        : root_(std::move(root)), max_blob_bytes_(max_blob_bytes) {}

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    [[nodiscard]] std::size_t max_blob_bytes() const noexcept { return max_blob_bytes_; }
    [[nodiscard]] std::filesystem::path path_for(const AssetId& id) const;
    [[nodiscard]] AssetCacheStoreResult store(std::span<const std::byte> bytes) const;
    [[nodiscard]] AssetCacheLoadResult load(const AssetId& id) const;

  private:
    std::filesystem::path root_;
    std::size_t max_blob_bytes_;
  };

}  // namespace mobagen::assets
