#include "plugin_activation_set.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace mobagen::plugins {
  namespace {

    void append_action_issues(ResolvedNativePluginActionResult& result, std::string_view provider_id, NativePluginActionResult action) {
      if (action.ok()) return;
      result.issues.push_back(
          {ResolvedNativePluginIssueCode::ShutdownFailed, std::string{provider_id}, "plugin shutdown reported failures", std::move(action.issues)});
    }

    ResolvedNativePluginActionResult stop_all(std::vector<std::unique_ptr<NativePluginActivation>>& activations) {
      ResolvedNativePluginActionResult result;
      for (auto activation = activations.rbegin(); activation != activations.rend(); ++activation) {
        if ((*activation)->state() == NativePluginActivationState::Active) {
          const auto provider = std::string{(*activation)->provider_id()};
          append_action_issues(result, provider, (*activation)->quiesce());
        }
      }
      for (auto activation = activations.rbegin(); activation != activations.rend(); ++activation) {
        if ((*activation)->state() == NativePluginActivationState::Quiesced) {
          const auto provider = std::string{(*activation)->provider_id()};
          append_action_issues(result, provider, (*activation)->stop());
        }
      }
      return result;
    }

    bool resolution_matches(const modules::CapabilityRegistry& registry, const modules::ModuleResolution& resolution) {
      if (registry.generation() != resolution.registry_generation()) return false;
      std::vector<bool> selected(registry.provider_count());
      for (const auto& selection : resolution.selections()) {
        const auto* provider = registry.provider(selection.provider);
        const auto capability = registry.capability_name(selection.capability);
        if (provider == nullptr || capability.empty() || selection.provider.value >= selected.size()
            || std::ranges::find(provider->provides, capability) == provider->provides.end()
            || std::ranges::find(provider->linkages, selection.linkage) == provider->linkages.end()) {
          return false;
        }
        selected[selection.provider.value] = true;
      }
      std::vector<bool> ordered(registry.provider_count());
      for (const auto provider : resolution.lifecycle_order()) {
        if (provider.value >= ordered.size() || ordered[provider.value] || !selected[provider.value]) return false;
        ordered[provider.value] = true;
      }
      return selected == ordered;
    }

  }  // namespace

  ResolvedNativePluginActivation::ResolvedNativePluginActivation(std::vector<std::unique_ptr<NativePluginActivation>> activations)
      : activations_(std::move(activations)) {}

  ResolvedNativePluginActivation::~ResolvedNativePluginActivation() {
    while (!activations_.empty()) activations_.pop_back();
  }

  ResolvedNativePluginActionResult ResolvedNativePluginActivation::stop() { return stop_all(activations_); }

  ResolvedNativePluginActivationResult activate_resolved_native_plugins(NativePluginCatalog& catalog, const modules::ModuleResolution& resolution,
                                                                        PluginHost& host) {
    ResolvedNativePluginActivationResult result;
    if (!resolution_matches(catalog.registry(), resolution)) {
      result.issues.push_back(
          {ResolvedNativePluginIssueCode::InvalidResolution, {}, "module resolution does not belong to the plugin catalog registry", {}});
      return result;
    }
    std::vector<std::unique_ptr<NativePluginActivation>> activations;
    activations.reserve(resolution.lifecycle_order().size());

    for (const auto provider_index : resolution.lifecycle_order()) {
      const auto* provider = catalog.registry().provider(provider_index);
      if (provider == nullptr) {
        result.issues.push_back(
            {ResolvedNativePluginIssueCode::InvalidResolution, {}, "resolved provider index is not part of the plugin catalog", {}});
        (void)stop_all(activations);
        return result;
      }
      auto plugin = catalog.take_plugin(provider->id);
      if (!plugin.has_value()) continue;

      auto activated = activate_loaded_native_plugin(std::move(*plugin), host);
      if (!activated.ok()) {
        result.issues.push_back({ResolvedNativePluginIssueCode::ActivationFailed, provider->id, "resolved native plugin failed to activate",
                                 std::move(activated.issues)});
        auto rollback = stop_all(activations);
        result.issues.insert(result.issues.end(), std::make_move_iterator(rollback.issues.begin()), std::make_move_iterator(rollback.issues.end()));
        return result;
      }
      activations.push_back(std::move(activated.activation));
    }

    result.activation = std::unique_ptr<ResolvedNativePluginActivation>(new ResolvedNativePluginActivation(std::move(activations)));
    return result;
  }

}  // namespace mobagen::plugins
