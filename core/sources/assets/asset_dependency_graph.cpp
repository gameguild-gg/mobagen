#include "asset_dependency_graph.hpp"

#include <algorithm>
#include <cstddef>
#include <queue>
#include <unordered_set>
#include <utility>

namespace mobagen::assets {
  namespace {

    using EdgeMap = std::unordered_map<AssetId, std::vector<AssetId>, AssetIdHash>;

    struct AssetIdGreater {
      [[nodiscard]] bool operator()(const AssetId& lhs, const AssetId& rhs) const noexcept { return lhs.bytes > rhs.bytes; }
    };

    [[nodiscard]] bool is_acyclic(const EdgeMap& graph) {
      std::unordered_map<AssetId, std::size_t, AssetIdHash> dependency_counts;
      std::unordered_map<AssetId, std::vector<AssetId>, AssetIdHash> dependents;
      dependency_counts.reserve(graph.size());
      dependents.reserve(graph.size());

      for (const auto& [asset, dependencies] : graph) {
        dependency_counts.emplace(asset, dependencies.size());
        for (const auto& dependency : dependencies) {
          dependents[dependency].push_back(asset);
        }
      }

      std::vector<AssetId> ready;
      ready.reserve(graph.size());
      for (const auto& [asset, count] : dependency_counts) {
        if (count == 0) {
          ready.push_back(asset);
        }
      }

      std::size_t processed = 0;
      while (!ready.empty()) {
        const auto asset = ready.back();
        ready.pop_back();
        ++processed;
        if (const auto found = dependents.find(asset); found != dependents.end()) {
          for (const auto& dependent : found->second) {
            auto& remaining = dependency_counts.at(dependent);
            --remaining;
            if (remaining == 0) {
              ready.push_back(dependent);
            }
          }
        }
      }
      return processed == graph.size();
    }

  }  // namespace

  bool AssetDependencyGraph::register_asset(const AssetId& asset) { return dependencies_.try_emplace(asset).second; }

  bool AssetDependencyGraph::contains(const AssetId& asset) const { return dependencies_.contains(asset); }

  AssetDependencyStatus AssetDependencyGraph::set_dependencies(const AssetId& asset, std::span<const AssetId> dependencies) {
    if (!contains(asset)) {
      return AssetDependencyStatus::unknown_asset;
    }

    std::unordered_set<AssetId, AssetIdHash> unique;
    unique.reserve(dependencies.size());
    std::vector<AssetId> canonical;
    canonical.reserve(dependencies.size());
    for (const auto& dependency : dependencies) {
      if (!contains(dependency)) {
        return AssetDependencyStatus::unknown_dependency;
      }
      if (dependency == asset) {
        return AssetDependencyStatus::self_dependency;
      }
      if (!unique.insert(dependency).second) {
        return AssetDependencyStatus::duplicate_dependency;
      }
      canonical.push_back(dependency);
    }
    std::sort(canonical.begin(), canonical.end(), [](const AssetId& lhs, const AssetId& rhs) { return lhs.bytes < rhs.bytes; });

    auto staged = dependencies_;
    staged.at(asset) = std::move(canonical);
    if (!is_acyclic(staged)) {
      return AssetDependencyStatus::cycle;
    }
    dependencies_.swap(staged);
    return AssetDependencyStatus::success;
  }

  AssetBuildOrderResult AssetDependencyGraph::build_order(std::span<const AssetId> roots) const {
    std::unordered_set<AssetId, AssetIdHash> closure;
    closure.reserve(dependencies_.size());
    std::vector<AssetId> pending{roots.begin(), roots.end()};
    while (!pending.empty()) {
      const auto asset = pending.back();
      pending.pop_back();
      const auto found = dependencies_.find(asset);
      if (found == dependencies_.end()) {
        return {AssetDependencyStatus::unknown_asset, {}};
      }
      if (!closure.insert(asset).second) {
        continue;
      }
      pending.insert(pending.end(), found->second.begin(), found->second.end());
    }

    std::unordered_map<AssetId, std::size_t, AssetIdHash> dependency_counts;
    std::unordered_map<AssetId, std::vector<AssetId>, AssetIdHash> dependents;
    dependency_counts.reserve(closure.size());
    dependents.reserve(closure.size());
    for (const auto& asset : closure) {
      const auto& dependencies = dependencies_.at(asset);
      dependency_counts.emplace(asset, dependencies.size());
      for (const auto& dependency : dependencies) {
        dependents[dependency].push_back(asset);
      }
    }

    std::priority_queue<AssetId, std::vector<AssetId>, AssetIdGreater> ready;
    for (const auto& [asset, count] : dependency_counts) {
      if (count == 0) {
        ready.push(asset);
      }
    }

    std::vector<AssetId> order;
    order.reserve(closure.size());
    while (!ready.empty()) {
      const auto asset = ready.top();
      ready.pop();
      order.push_back(asset);
      if (const auto found = dependents.find(asset); found != dependents.end()) {
        for (const auto& dependent : found->second) {
          auto& remaining = dependency_counts.at(dependent);
          --remaining;
          if (remaining == 0) {
            ready.push(dependent);
          }
        }
      }
    }

    if (order.size() != closure.size()) {
      return {AssetDependencyStatus::cycle, {}};
    }
    return {AssetDependencyStatus::success, std::move(order)};
  }

}  // namespace mobagen::assets
