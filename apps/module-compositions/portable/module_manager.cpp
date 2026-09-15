#include "module_manager.hpp"

#include "project_support.hpp"

#include "assets/asset_id.hpp"

#include <algorithm>
#include <map>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <utility>

namespace mobagen::compositions {
  namespace {

    bool descriptor_matches(
        const modules::LockedPluginActivationEntry& expected,
        const modules::ProviderDescriptor& actual
    ) {
      if (actual.id != expected.provider_id || actual.version != expected.version
          || std::ranges::find(actual.linkages, modules::LinkageMode::Wasm)
                 == actual.linkages.end()
          || (!expected.configuration_schema.empty()
              && actual.configuration_schema != expected.configuration_schema)) {
        return false;
      }
      return std::ranges::all_of(expected.capabilities, [&](const auto& capability) {
        return std::ranges::find(actual.provides, capability) != actual.provides.end();
      });
    }

    bool permissions_are_granted(
        const modules::ProviderDescriptor& provider,
        std::span<const std::string> granted_permissions
    ) {
      return std::ranges::all_of(provider.permissions, [&](const auto& permission) {
        return std::ranges::binary_search(granted_permissions, permission);
      });
    }

    void append_shutdown_issues(
        PortableModuleManagerActionResult& result, std::string_view provider_id,
        plugins::PortableWasmPluginActionResult action
    ) {
      if (action.ok()) return;
      result.issues.push_back({
          .code = PortableModuleManagerIssueCode::ShutdownFailed,
          .provider_id = std::string{provider_id},
          .message = "portable WASM module shutdown reported failures",
          .activation_issues = std::move(action.issues),
      });
    }

  }  // namespace

  PortableModuleManager::PortableModuleManager(
      std::unique_ptr<modules::LockedPluginActivationPlan> plan,
      plugins::PortableWasmBackend& backend,
      std::vector<std::vector<std::byte>> configurations,
      std::vector<modules::ProviderDescriptor> builtin_providers,
      std::vector<std::string> granted_permissions, plugins::WasmHostServices host_services
  )
      : plan_(std::move(plan)),
        backend_(&backend),
        host_services_(host_services),
        owner_thread_(std::this_thread::get_id()),
        configurations_(std::move(configurations)),
        builtin_providers_(std::move(builtin_providers)),
        granted_permissions_(std::move(granted_permissions)) {
    const auto entries = plan_->entries();
    dependencies_.resize(entries.size());
    activations_.resize(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
      providers_.emplace(entries[index].provider_id, index);
      for (const auto& capability : entries[index].capabilities) {
        capabilities_.emplace(capability, index);
      }
    }
    for (std::size_t index = 0; index < entries.size(); ++index) {
      for (const auto& dependency : entries[index].dependencies) {
        dependencies_[index].push_back(providers_.at(dependency.provider_id));
      }
    }
  }

  PortableModuleManager::~PortableModuleManager() {
    for (auto activation = activation_order_.rbegin(); activation != activation_order_.rend();
         ++activation) {
      activations_[*activation].reset();
    }
  }

  plugins::PortableWasmPluginActivation* PortableModuleManager::plugin(
      std::string_view provider_id
  ) noexcept {
    const auto found = providers_.find(provider_id);
    return found == providers_.end() ? nullptr : activations_[found->second].get();
  }

  const plugins::PortableWasmPluginActivation* PortableModuleManager::plugin(
      std::string_view provider_id
  ) const noexcept {
    const auto found = providers_.find(provider_id);
    return found == providers_.end() ? nullptr : activations_[found->second].get();
  }

  void PortableModuleManager::collect_inactive_closure(
      std::size_t index, std::vector<bool>& visited, std::vector<std::size_t>& closure
  ) const {
    if (visited[index] || activations_[index] != nullptr) return;
    visited[index] = true;
    for (const auto dependency : dependencies_[index]) {
      collect_inactive_closure(dependency, visited, closure);
    }
    closure.push_back(index);
  }

  void PortableModuleManager::rollback(
      std::vector<std::size_t>& activated, PortableModuleManagerActionResult& result
  ) {
    for (auto index = activated.rbegin(); index != activated.rend(); ++index) {
      auto& activation = activations_[*index];
      if (activation != nullptr
          && activation->state() == plugins::PortableWasmPluginState::Active) {
        append_shutdown_issues(result, activation->provider().id, activation->quiesce());
      }
    }
    for (auto index = activated.rbegin(); index != activated.rend(); ++index) {
      auto& activation = activations_[*index];
      if (activation != nullptr
          && activation->state() == plugins::PortableWasmPluginState::Quiesced) {
        append_shutdown_issues(result, activation->provider().id, activation->stop());
      }
      if (activation != nullptr
          && activation->state() == plugins::PortableWasmPluginState::Stopped) {
        activation.reset();
        --active_count_;
        const auto found = std::ranges::find(activation_order_, *index);
        if (found != activation_order_.end()) activation_order_.erase(found);
      }
    }
  }

  PortableModuleManagerActionResult PortableModuleManager::activate(
      std::string_view capability
  ) {
    PortableModuleManagerActionResult result;
    if (owner_thread_ != std::this_thread::get_id()) {
      result.issues.push_back({
          .code = PortableModuleManagerIssueCode::WrongThread,
          .message = "portable WASM modules must be activated on the manager owner thread",
      });
      return result;
    }
    const auto selected = capabilities_.find(capability);
    if (selected == capabilities_.end()) {
      result.issues.push_back({
          .code = PortableModuleManagerIssueCode::UnknownCapability,
          .message = "capability is not provided by the locked portable module plan",
      });
      return result;
    }
    if (activations_[selected->second] != nullptr) return result;

    std::vector<bool> visited(plan_->entries().size());
    std::vector<std::size_t> closure;
    collect_inactive_closure(selected->second, visited, closure);
    if (closure.empty()) return result;

    std::vector<std::optional<plugins::LoadedPortableWasmPlugin>> loaded(
        plan_->entries().size()
    );
    for (const auto index : closure) {
      const auto& entry = plan_->entries()[index];
      if (entry.linkage != modules::LinkageMode::Wasm) {
        result.issues.push_back({
            .code = PortableModuleManagerIssueCode::UnsupportedLinkage,
            .provider_id = entry.provider_id,
            .message = "portable module manager can activate only WASM plugins",
        });
        return result;
      }
      if (!entry.binary_hash.empty()) {
        const auto hash = detail::hash_project_plugin_binary(
            entry.binary_path, plugins::max_portable_wasm_plugin_binary_bytes
        );
        if (!hash.ok() || *hash.hash != entry.binary_hash) {
          result.issues.push_back({
              .code = PortableModuleManagerIssueCode::ArtifactVerificationFailed,
              .provider_id = entry.provider_id,
              .message = hash.ok()
                           ? "selected portable plugin no longer matches mobagen.lock"
                           : "selected portable plugin could not be verified: " + hash.error,
          });
          return result;
        }
      }
      auto candidate = plugins::load_portable_wasm_plugin_binary(
          entry.binary_path, *backend_, host_services_
      );
      if (!candidate.plugin.has_value()) {
        result.issues.push_back({
            .code = PortableModuleManagerIssueCode::LoadFailed,
            .provider_id = entry.provider_id,
            .message = "selected portable WASM plugin could not be loaded",
            .load_issues = std::move(candidate.issues),
        });
        return result;
      }
      if (!descriptor_matches(entry, candidate.plugin->provider())) {
        result.issues.push_back({
            .code = PortableModuleManagerIssueCode::DescriptorMismatch,
            .provider_id = entry.provider_id,
            .message = "loaded WASM plugin descriptor does not match the locked provider",
        });
        return result;
      }
      if (!permissions_are_granted(candidate.plugin->provider(), granted_permissions_)) {
        result.issues.push_back({
            .code = PortableModuleManagerIssueCode::PermissionDenied,
            .provider_id = entry.provider_id,
            .message = "loaded WASM plugin requests a permission absent from mobagen.lock",
        });
        return result;
      }
      loaded[index] = std::move(*candidate.plugin);
    }

    modules::CapabilityRegistryBuilder registry_builder;
    for (const auto& builtin : builtin_providers_) registry_builder.add(builtin);
    for (const auto& activation : activations_) {
      if (activation != nullptr) registry_builder.add(activation->provider());
    }
    for (const auto index : closure) registry_builder.add(loaded[index]->provider());
    auto built = registry_builder.build();
    if (!built.ok()) {
      result.issues.push_back({
          .code = PortableModuleManagerIssueCode::RegistryFailed,
          .message = "portable WASM capability registry could not be built",
          .registry_issues = std::move(built.issues),
      });
      return result;
    }
    std::shared_ptr<const modules::CapabilityRegistry> registry;
    try {
      registry = std::make_shared<const modules::CapabilityRegistry>(
          std::move(*built.registry)
      );
    } catch (const std::bad_alloc&) {
      result.issues.push_back({
          .code = PortableModuleManagerIssueCode::RegistryFailed,
          .message = "portable WASM capability registry allocation failed",
      });
      return result;
    }

    std::vector<std::size_t> activated;
    for (const auto index : closure) {
      const auto& entry = plan_->entries()[index];
      auto activation = plugins::activate_loaded_portable_wasm_plugin(
          std::move(*loaded[index]), registry, configurations_[index]
      );
      if (!activation.ok()) {
        result.issues.push_back({
            .code = PortableModuleManagerIssueCode::ActivationFailed,
            .provider_id = entry.provider_id,
            .message = "selected portable WASM plugin failed to activate",
            .activation_issues = std::move(activation.issues),
        });
        rollback(activated, result);
        return result;
      }
      activations_[index] = std::move(activation.activation);
      activation_order_.push_back(index);
      activated.push_back(index);
      ++active_count_;
    }
    return result;
  }

  PortableModuleCapabilityResult PortableModuleManager::acquire(
      std::string_view capability
  ) {
    PortableModuleCapabilityResult result;
    auto activated = activate(capability);
    if (!activated.ok()) {
      result.issues = std::move(activated.issues);
      return result;
    }
    const auto selected = capabilities_.find(capability);
    if (selected != capabilities_.end()) {
      result.plugin = activations_[selected->second].get();
    }
    if (result.plugin == nullptr) {
      result.issues.push_back({
          .code = PortableModuleManagerIssueCode::CapabilityUnavailable,
          .message = "portable capability has no active WASM endpoint",
      });
    }
    return result;
  }

  PortableModuleManagerActionResult PortableModuleManager::stop() {
    PortableModuleManagerActionResult result;
    if (owner_thread_ != std::this_thread::get_id()) {
      result.issues.push_back({
          .code = PortableModuleManagerIssueCode::WrongThread,
          .message = "portable WASM modules must be stopped on the manager owner thread",
      });
      return result;
    }
    for (auto index = activation_order_.rbegin(); index != activation_order_.rend(); ++index) {
      auto& activation = activations_[*index];
      if (activation->state() == plugins::PortableWasmPluginState::Active) {
        append_shutdown_issues(result, activation->provider().id, activation->quiesce());
      }
    }
    for (auto index = activation_order_.rbegin(); index != activation_order_.rend(); ++index) {
      auto& activation = activations_[*index];
      if (activation->state() == plugins::PortableWasmPluginState::Quiesced) {
        append_shutdown_issues(result, activation->provider().id, activation->stop());
      }
    }
    std::erase_if(activation_order_, [&](const auto index) {
      auto& activation = activations_[index];
      if (activation->state() != plugins::PortableWasmPluginState::Stopped) return false;
      activation.reset();
      --active_count_;
      return true;
    });
    return result;
  }

  PortableModuleManagerCreateResult create_portable_module_manager(
      std::unique_ptr<modules::LockedPluginActivationPlan> plan,
      plugins::PortableWasmBackend& backend,
      std::span<const PortableModuleConfiguration> configurations,
      std::span<const modules::ProviderDescriptor> builtin_providers,
      std::span<const std::string> granted_permissions,
      plugins::WasmHostServices host_services
  ) {
    PortableModuleManagerCreateResult result;
    if (plan == nullptr) {
      result.issues.push_back({
          .code = PortableModuleManagerIssueCode::InvalidPlan,
          .message = "portable module manager requires a locked activation plan",
      });
      return result;
    }

    std::map<std::string, std::span<const std::byte>, std::less<>> supplied;
    for (const auto& configuration : configurations) {
      if (!supplied.emplace(
              configuration.provider_id, std::span<const std::byte>{configuration.data}
          ).second) {
        result.issues.push_back({
            .code = PortableModuleManagerIssueCode::InvalidConfiguration,
            .provider_id = configuration.provider_id,
            .message = "portable module configuration providers must be unique",
        });
        return result;
      }
    }
    std::vector<std::vector<std::byte>> owned_configurations;
    owned_configurations.reserve(plan->entries().size());
    for (const auto& entry : plan->entries()) {
      const auto configuration = supplied.find(entry.provider_id);
      if (entry.configuration_hash.empty()) {
        if (configuration != supplied.end()) {
          result.issues.push_back({
              .code = PortableModuleManagerIssueCode::InvalidConfiguration,
              .provider_id = entry.provider_id,
              .message = "configuration was supplied for an unconfigured locked plugin",
          });
          return result;
        }
        owned_configurations.emplace_back();
        continue;
      }
      if (configuration == supplied.end()) {
        result.issues.push_back({
            .code = PortableModuleManagerIssueCode::InvalidConfiguration,
            .provider_id = entry.provider_id,
            .message = "locked portable plugin configuration bytes are missing",
        });
        return result;
      }
      const auto digest = assets::sha256(configuration->second);
      if (!digest.has_value() || assets::to_string(*digest) != entry.configuration_hash) {
        result.issues.push_back({
            .code = PortableModuleManagerIssueCode::InvalidConfiguration,
            .provider_id = entry.provider_id,
            .message = "portable module configuration does not match mobagen.lock",
        });
        return result;
      }
      owned_configurations.emplace_back(
          configuration->second.begin(), configuration->second.end()
      );
      supplied.erase(configuration);
    }
    if (!supplied.empty()) {
      result.issues.push_back({
          .code = PortableModuleManagerIssueCode::InvalidConfiguration,
          .provider_id = supplied.begin()->first,
          .message = "configuration provider is not part of the portable activation plan",
      });
      return result;
    }

    std::vector<std::string> owned_permissions{
        granted_permissions.begin(), granted_permissions.end()
    };
    std::ranges::sort(owned_permissions);
    if (std::ranges::adjacent_find(owned_permissions) != owned_permissions.end()) {
      result.issues.push_back({
          .code = PortableModuleManagerIssueCode::InvalidPermissionGrant,
          .message = "portable module permission grants must be unique",
      });
      return result;
    }

    std::vector<modules::ProviderDescriptor> owned_builtins{
        builtin_providers.begin(), builtin_providers.end()
    };
    modules::CapabilityRegistryBuilder builtin_registry;
    for (const auto& provider : owned_builtins) builtin_registry.add(provider);
    auto built = builtin_registry.build();
    if (!built.ok()) {
      result.issues.push_back({
          .code = PortableModuleManagerIssueCode::InvalidPlan,
          .message = "portable module built-in providers are invalid",
          .registry_issues = std::move(built.issues),
      });
      return result;
    }
    for (const auto& entry : plan->entries()) {
      if (built.registry->find_provider(entry.provider_id).has_value()) {
        result.issues.push_back({
            .code = PortableModuleManagerIssueCode::InvalidPlan,
            .provider_id = entry.provider_id,
            .message = "portable plugin provider conflicts with a built-in provider",
        });
        return result;
      }
    }

    result.manager = std::unique_ptr<PortableModuleManager>(new PortableModuleManager(
        std::move(plan), backend, std::move(owned_configurations),
        std::move(owned_builtins), std::move(owned_permissions), host_services
    ));
    return result;
  }

}  // namespace mobagen::compositions
