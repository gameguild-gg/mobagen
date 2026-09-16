#pragma once

#include "asset_id.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace mobagen::assets {

  enum class AssetDependencyStatus : std::uint8_t {
    success,
    unknown_asset,
    unknown_dependency,
    self_dependency,
    duplicate_dependency,
    cycle,
  };

  struct AssetBuildOrderResult {
    AssetDependencyStatus status{AssetDependencyStatus::success};
    std::vector<AssetId> assets;
  };

  class AssetDependencyGraph {
  public:
    [[nodiscard]] bool register_asset(const AssetId& asset);
    [[nodiscard]] bool contains(const AssetId& asset) const;

    [[nodiscard]] AssetDependencyStatus set_dependencies(const AssetId& asset, std::span<const AssetId> dependencies);
    [[nodiscard]] AssetBuildOrderResult build_order(std::span<const AssetId> roots) const;

    [[nodiscard]] std::size_t size() const noexcept { return dependencies_.size(); }

  private:
    using EdgeMap = std::unordered_map<AssetId, std::vector<AssetId>, AssetIdHash>;
    EdgeMap dependencies_;
  };

}  // namespace mobagen::assets
