#include "locked_activation_plan.hpp"

#include "assets/asset_id.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace mobagen::modules {
  namespace {

    struct SelectedProvider {
      SemanticVersion version;
      LinkageMode linkage{};
      std::vector<std::string> capabilities;
    };

    LockedActivationPlanResult failure(LockedActivationPlanIssueCode code, std::string provider_id, std::string message) {
      LockedActivationPlanResult result;
      result.issues.push_back({code, std::move(provider_id), std::move(message)});
      return result;
    }

    bool is_plugin_linkage(LinkageMode linkage) { return linkage == LinkageMode::Dynamic || linkage == LinkageMode::Wasm; }

  }  // namespace

  const LockedPluginActivationEntry* LockedPluginActivationPlan::find(std::string_view provider_id) const noexcept {
    const auto found = std::ranges::find(entries_, provider_id, &LockedPluginActivationEntry::provider_id);
    return found == entries_.end() ? nullptr : &*found;
  }

  LockedActivationPlanResult build_locked_plugin_activation_plan(const LockfileDocument& document,
                                                                 std::span<const VerifiedLockedPlugin> verified_plugins) {
    std::map<std::string, SelectedProvider, std::less<>> selected;
    std::map<std::string, std::string, std::less<>> selected_capabilities;
    for (const auto& selection : document.resolved) {
      if (!selected_capabilities.emplace(selection.capability, selection.provider).second) {
        return failure(LockedActivationPlanIssueCode::InvalidSelection, selection.provider, "locked capabilities must be selected exactly once");
      }
      const auto [found, inserted]
          = selected.try_emplace(selection.provider, SelectedProvider{selection.version, selection.linkage, {selection.capability}});
      if (!inserted) {
        if (found->second.version != selection.version || found->second.linkage != selection.linkage) {
          return failure(LockedActivationPlanIssueCode::InvalidSelection, selection.provider,
                         "one locked provider has conflicting versions or linkages");
        }
        found->second.capabilities.push_back(selection.capability);
      }
    }

    std::map<std::string, const VerifiedLockedPlugin*, std::less<>> verified;
    for (const auto& plugin : verified_plugins) {
      if (!verified.emplace(plugin.provider_id, &plugin).second) {
        return failure(LockedActivationPlanIssueCode::UnexpectedVerifiedPlugin, plugin.provider_id, "verified plugin providers must be unique");
      }
    }

    std::map<std::string, const PluginLockEntry*, std::less<>> locked;
    for (const auto& plugin : document.metadata.plugins) {
      if (!locked.emplace(plugin.provider, &plugin).second) {
        return failure(LockedActivationPlanIssueCode::InvalidSelection, plugin.provider, "locked plugin providers must be unique");
      }
      const auto selection = selected.find(plugin.provider);
      if (selection == selected.end() || !is_plugin_linkage(selection->second.linkage)) {
        return failure(LockedActivationPlanIssueCode::InvalidSelection, plugin.provider,
                       "locked plugin is not selected with a loadable runtime linkage");
      }
      if (selection->second.version != plugin.version) {
        return failure(LockedActivationPlanIssueCode::MetadataMismatch, plugin.provider,
                       "locked plugin version does not match its capability selections");
      }
      const auto verified_plugin = verified.find(plugin.provider);
      if (verified_plugin == verified.end()) {
        return failure(LockedActivationPlanIssueCode::MissingVerifiedPlugin, plugin.provider, "selected plugin has no verified package");
      }
      const auto& candidate = *verified_plugin->second;
      if (candidate.version != plugin.version || candidate.linkage != selection->second.linkage || candidate.abi_version != plugin.abi_version) {
        return failure(LockedActivationPlanIssueCode::MetadataMismatch, plugin.provider, "verified plugin metadata does not match mobagen.lock");
      }
    }

    for (const auto& [provider, selection] : selected) {
      if (is_plugin_linkage(selection.linkage) && !locked.contains(provider)) {
        return failure(LockedActivationPlanIssueCode::MissingVerifiedPlugin, provider,
                       "selected runtime plugin is absent from the lockfile plugin set");
      }
    }
    for (const auto& [provider, plugin] : verified) {
      (void)plugin;
      if (!locked.contains(provider)) {
        return failure(LockedActivationPlanIssueCode::UnexpectedVerifiedPlugin, provider, "verified package is not selected by mobagen.lock");
      }
    }

    std::map<std::string, const LockedConfiguration*, std::less<>> configurations;
    for (const auto& configuration : document.configurations) {
      if (!selected.contains(configuration.provider) || !configurations.emplace(configuration.provider, &configuration).second) {
        return failure(LockedActivationPlanIssueCode::InvalidSelection, configuration.provider,
                       "locked configurations must uniquely reference selected providers");
      }
    }

    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> dependents;
    std::map<std::string, std::size_t, std::less<>> indegree;
    std::map<std::string, std::vector<LockedPluginDependency>, std::less<>> plugin_dependencies;
    for (const auto& [provider, selection] : selected) {
      (void)selection;
      dependents.try_emplace(provider);
      indegree.try_emplace(provider, 0);
    }
    for (const auto& dependency : document.dependencies) {
      const auto selected_capability = selected_capabilities.find(dependency.capability);
      if (dependency.provider == dependency.required_by || !selected.contains(dependency.provider) || !selected.contains(dependency.required_by)
          || selected_capability == selected_capabilities.end() || selected_capability->second != dependency.provider) {
        return failure(LockedActivationPlanIssueCode::InvalidDependency, dependency.required_by,
                       "locked dependency does not reference a coherent selected provider");
      }
      if (dependents[dependency.provider].insert(dependency.required_by).second) {
        ++indegree[dependency.required_by];
      }
      if (locked.contains(dependency.provider) && locked.contains(dependency.required_by)) {
        plugin_dependencies[dependency.required_by].push_back({
            .provider_id = dependency.provider,
            .capability = dependency.capability,
        });
      }
    }

    std::set<std::string, std::less<>> ready;
    for (const auto& [provider, count] : indegree) {
      if (count == 0) ready.insert(provider);
    }
    std::vector<std::string> lifecycle;
    lifecycle.reserve(selected.size());
    while (!ready.empty()) {
      auto next = ready.extract(ready.begin()).value();
      lifecycle.push_back(next);
      for (const auto& dependent : dependents[next]) {
        auto& count = indegree[dependent];
        --count;
        if (count == 0) ready.insert(dependent);
      }
    }
    if (lifecycle.size() != selected.size()) {
      return failure(LockedActivationPlanIssueCode::DependencyCycle, {}, "locked provider dependencies contain a cycle");
    }

    std::vector<LockedPluginActivationEntry> entries;
    entries.reserve(locked.size());
    for (const auto& provider : lifecycle) {
      const auto lock_entry = locked.find(provider);
      if (lock_entry == locked.end()) continue;
      const auto& candidate = *verified.at(provider);
      auto capabilities = selected.at(provider).capabilities;
      std::ranges::sort(capabilities);
      auto dependencies = std::move(plugin_dependencies[provider]);
      std::ranges::sort(dependencies, [](const auto& left, const auto& right) {
        return std::tie(left.provider_id, left.capability) < std::tie(right.provider_id, right.capability);
      });
      const auto configuration = configurations.find(provider);
      entries.push_back({
          .provider_id = provider,
          .version = candidate.version,
          .linkage = candidate.linkage,
          .abi_version = candidate.abi_version,
          .size = candidate.size,
          .package_path = candidate.package_path,
          .binary_path = candidate.binary_path,
          .capabilities = std::move(capabilities),
          .dependencies = std::move(dependencies),
          .configuration_schema = configuration == configurations.end() ? std::string{} : configuration->second->schema,
          .configuration_hash = configuration == configurations.end() ? std::string{} : configuration->second->hash,
      });
    }

    LockedActivationPlanResult result;
    result.plan = std::unique_ptr<LockedPluginActivationPlan>(new LockedPluginActivationPlan(std::move(entries)));
    return result;
  }

  LockedActivationPlanResult build_locked_plugin_activation_plan(const LockfileDocument& document,
                                                                 std::span<const StagedLockedPlugin> staged_plugins) {
    std::vector<VerifiedLockedPlugin> metadata;
    metadata.reserve(staged_plugins.size());
    std::map<std::string, std::string, std::less<>> hashes;
    for (const auto& plugin : staged_plugins) {
      if (!assets::parse_asset_id(plugin.expected_hash).has_value() || !hashes.emplace(plugin.provider_id, plugin.expected_hash).second) {
        return failure(LockedActivationPlanIssueCode::MetadataMismatch, plugin.provider_id, "staged plugin hash metadata is invalid or duplicated");
      }
      metadata.push_back({
          .provider_id = plugin.provider_id,
          .version = plugin.version,
          .linkage = plugin.linkage,
          .abi_version = plugin.abi_version,
          .package_path = plugin.package_path,
          .binary_path = plugin.binary_path,
      });
    }
    auto result = build_locked_plugin_activation_plan(document, metadata);
    if (!result.ok()) return result;
    for (auto& entry : result.plan->entries_) {
      entry.binary_hash = hashes.at(entry.provider_id);
    }
    return result;
  }

}  // namespace mobagen::modules
