#include "wasm_plugin_activation_set.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>

namespace mobagen::plugins {
  namespace {

    void append_action_issues(ResolvedPortableWasmPluginActionResult& result, std::string_view provider_id, PortableWasmPluginActionResult action) {
      if (action.ok()) return;
      result.issues.push_back({ResolvedPortableWasmPluginIssueCode::ShutdownFailed, std::string{provider_id},
                               "portable WASM plugin shutdown reported failures", std::move(action.issues)});
    }

    ResolvedPortableWasmPluginActionResult stop_all(std::vector<std::unique_ptr<PortableWasmPluginActivation>>& activations) {
      ResolvedPortableWasmPluginActionResult result;
      for (auto activation = activations.rbegin(); activation != activations.rend(); ++activation) {
        if ((*activation)->state() == PortableWasmPluginState::Active) {
          const auto provider = (*activation)->provider().id;
          append_action_issues(result, provider, (*activation)->quiesce());
        }
      }
      for (auto activation = activations.rbegin(); activation != activations.rend(); ++activation) {
        if ((*activation)->state() == PortableWasmPluginState::Quiesced) {
          const auto provider = (*activation)->provider().id;
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

  ResolvedPortableWasmPluginActivation::ResolvedPortableWasmPluginActivation(std::vector<std::unique_ptr<PortableWasmPluginActivation>> activations)
      : activations_(std::move(activations)) {}

  ResolvedPortableWasmPluginActivation::~ResolvedPortableWasmPluginActivation() {
    while (!activations_.empty()) activations_.pop_back();
  }

  PortableWasmPluginActivation* ResolvedPortableWasmPluginActivation::plugin(std::size_t index) noexcept {
    return index < activations_.size() ? activations_[index].get() : nullptr;
  }

  const PortableWasmPluginActivation* ResolvedPortableWasmPluginActivation::plugin(std::size_t index) const noexcept {
    return index < activations_.size() ? activations_[index].get() : nullptr;
  }

  ResolvedPortableWasmPluginActionResult ResolvedPortableWasmPluginActivation::stop() { return stop_all(activations_); }

  ResolvedPortableWasmPluginActivationResult activate_resolved_portable_wasm_plugins(PortableWasmPluginCatalog& catalog,
                                                                                     const modules::ModuleResolution& resolution) {
    ResolvedPortableWasmPluginActivationResult result;
    if (!resolution_matches(catalog.registry(), resolution)) {
      result.issues.push_back({ResolvedPortableWasmPluginIssueCode::InvalidResolution,
                               {},
                               "module resolution does not belong to the portable WASM plugin catalog registry",
                               {}});
      return result;
    }
    std::vector<std::unique_ptr<PortableWasmPluginActivation>> activations;
    activations.reserve(resolution.lifecycle_order().size());

    for (const auto provider_index : resolution.lifecycle_order()) {
      const auto* provider = catalog.registry().provider(provider_index);
      if (provider == nullptr) {
        result.issues.push_back({ResolvedPortableWasmPluginIssueCode::InvalidResolution,
                                 {},
                                 "resolved provider index is not part of the portable WASM plugin catalog",
                                 {}});
        (void)stop_all(activations);
        return result;
      }
      auto plugin = catalog.take_plugin(provider->id);
      if (!plugin.has_value()) {
        if (catalog.is_plugin_provider(provider->id)) {
          result.issues.push_back({ResolvedPortableWasmPluginIssueCode::PluginUnavailable,
                                   provider->id,
                                   "resolved portable WASM plugin was already consumed from the catalog",
                                   {}});
          auto rollback = stop_all(activations);
          result.issues.insert(result.issues.end(), std::make_move_iterator(rollback.issues.begin()), std::make_move_iterator(rollback.issues.end()));
          return result;
        }
        continue;
      }

      std::span<const std::byte> configuration;
      if (const auto* resolved = resolution.configuration_for(provider_index)) {
        configuration = {reinterpret_cast<const std::byte*>(resolved->data.data()), resolved->data.size()};
      }
      auto activated = activate_loaded_portable_wasm_plugin(std::move(*plugin), configuration);
      if (!activated.ok()) {
        result.issues.push_back({ResolvedPortableWasmPluginIssueCode::ActivationFailed, provider->id,
                                 "resolved portable WASM plugin failed to activate", std::move(activated.issues)});
        auto rollback = stop_all(activations);
        result.issues.insert(result.issues.end(), std::make_move_iterator(rollback.issues.begin()), std::make_move_iterator(rollback.issues.end()));
        return result;
      }
      activations.push_back(std::move(activated.activation));
    }

    result.activation = std::unique_ptr<ResolvedPortableWasmPluginActivation>(new ResolvedPortableWasmPluginActivation(std::move(activations)));
    return result;
  }

}  // namespace mobagen::plugins
