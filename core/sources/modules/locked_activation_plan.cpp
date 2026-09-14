#include "locked_activation_plan.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace mobagen::modules {
  namespace {

    struct SelectedProvider {
      SemanticVersion version;
      LinkageMode linkage{};
      std::vector<std::string> capabilities;
    };

    LockedActivationPlanResult failure(
        LockedActivationPlanIssueCode code, std::string provider_id, std::string message
    ) {
      LockedActivationPlanResult result;
      result.issues.push_back({code, std::move(provider_id), std::move(message)});
      return result;
    }

    bool is_plugin_linkage(LinkageMode linkage) {
      return linkage == LinkageMode::Dynamic || linkage == LinkageMode::Wasm;
    }

  }  // namespace

  const LockedPluginActivationEntry* LockedPluginActivationPlan::find(
      std::string_view provider_id
  ) const noexcept {
    const auto found = std::ranges::find(entries_, provider_id,
                                         &LockedPluginActivationEntry::provider_id);
    return found == entries_.end() ? nullptr : &*found;
  }

  LockedActivationPlanResult build_locked_plugin_activation_plan(
      const LockfileDocument& document, std::span<const VerifiedLockedPlugin> verified_plugins
  ) {
    std::map<std::string, SelectedProvider, std::less<>> selected;
    for (const auto& selection : document.resolved) {
      const auto [found, inserted] = selected.try_emplace(
          selection.provider,
          SelectedProvider{selection.version, selection.linkage, {selection.capability}}
      );
      if (!inserted) {
        if (found->second.version != selection.version
            || found->second.linkage != selection.linkage) {
          return failure(
              LockedActivationPlanIssueCode::InvalidSelection, selection.provider,
              "one locked provider has conflicting versions or linkages"
          );
        }
        found->second.capabilities.push_back(selection.capability);
      }
    }

    std::map<std::string, const VerifiedLockedPlugin*, std::less<>> verified;
    for (const auto& plugin : verified_plugins) {
      if (!verified.emplace(plugin.provider_id, &plugin).second) {
        return failure(
            LockedActivationPlanIssueCode::UnexpectedVerifiedPlugin, plugin.provider_id,
            "verified plugin providers must be unique"
        );
      }
    }

    std::map<std::string, const PluginLockEntry*, std::less<>> locked;
    for (const auto& plugin : document.metadata.plugins) {
      if (!locked.emplace(plugin.provider, &plugin).second) {
        return failure(
            LockedActivationPlanIssueCode::InvalidSelection, plugin.provider,
            "locked plugin providers must be unique"
        );
      }
      const auto selection = selected.find(plugin.provider);
      if (selection == selected.end() || !is_plugin_linkage(selection->second.linkage)) {
        return failure(
            LockedActivationPlanIssueCode::InvalidSelection, plugin.provider,
            "locked plugin is not selected with a loadable runtime linkage"
        );
      }
      if (selection->second.version != plugin.version) {
        return failure(
            LockedActivationPlanIssueCode::MetadataMismatch, plugin.provider,
            "locked plugin version does not match its capability selections"
        );
      }
      const auto verified_plugin = verified.find(plugin.provider);
      if (verified_plugin == verified.end()) {
        return failure(
            LockedActivationPlanIssueCode::MissingVerifiedPlugin, plugin.provider,
            "selected plugin has no verified package"
        );
      }
      const auto& candidate = *verified_plugin->second;
      if (candidate.version != plugin.version
          || candidate.linkage != selection->second.linkage
          || candidate.abi_version != plugin.abi_version) {
        return failure(
            LockedActivationPlanIssueCode::MetadataMismatch, plugin.provider,
            "verified plugin metadata does not match mobagen.lock"
        );
      }
    }

    for (const auto& [provider, selection] : selected) {
      if (is_plugin_linkage(selection.linkage) && !locked.contains(provider)) {
        return failure(
            LockedActivationPlanIssueCode::MissingVerifiedPlugin, provider,
            "selected runtime plugin is absent from the lockfile plugin set"
        );
      }
    }
    for (const auto& [provider, plugin] : verified) {
      (void)plugin;
      if (!locked.contains(provider)) {
        return failure(
            LockedActivationPlanIssueCode::UnexpectedVerifiedPlugin, provider,
            "verified package is not selected by mobagen.lock"
        );
      }
    }

    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> dependents;
    std::map<std::string, std::size_t, std::less<>> indegree;
    for (const auto& [provider, selection] : selected) {
      (void)selection;
      dependents.try_emplace(provider);
      indegree.try_emplace(provider, 0);
    }
    for (const auto& dependency : document.dependencies) {
      const auto selected_capability = std::ranges::find(
          document.resolved, dependency.capability, &LockedProviderSelection::capability
      );
      if (dependency.provider == dependency.required_by
          || !selected.contains(dependency.provider)
          || !selected.contains(dependency.required_by)
          || selected_capability == document.resolved.end()
          || selected_capability->provider != dependency.provider) {
        return failure(
            LockedActivationPlanIssueCode::InvalidDependency, dependency.required_by,
            "locked dependency does not reference a coherent selected provider"
        );
      }
      if (dependents[dependency.provider].insert(dependency.required_by).second) {
        ++indegree[dependency.required_by];
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
      return failure(
          LockedActivationPlanIssueCode::DependencyCycle, {},
          "locked provider dependencies contain a cycle"
      );
    }

    std::vector<LockedPluginActivationEntry> entries;
    entries.reserve(locked.size());
    for (const auto& provider : lifecycle) {
      const auto lock_entry = locked.find(provider);
      if (lock_entry == locked.end()) continue;
      const auto& candidate = *verified.at(provider);
      auto capabilities = selected.at(provider).capabilities;
      std::ranges::sort(capabilities);
      entries.push_back({
          .provider_id = provider,
          .version = candidate.version,
          .linkage = candidate.linkage,
          .abi_version = candidate.abi_version,
          .size = candidate.size,
          .package_path = candidate.package_path,
          .binary_path = candidate.binary_path,
          .capabilities = std::move(capabilities),
      });
    }

    LockedActivationPlanResult result;
    result.plan = std::unique_ptr<LockedPluginActivationPlan>(
        new LockedPluginActivationPlan(std::move(entries))
    );
    return result;
  }

}  // namespace mobagen::modules
