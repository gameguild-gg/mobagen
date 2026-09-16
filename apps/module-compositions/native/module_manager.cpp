#include "module_manager.hpp"

#include "project_support.hpp"

#include "assets/asset_id.hpp"
#include "modules/catalog.hpp"
#include "plugins/plugin_loader.hpp"

#include <algorithm>
#include <map>
#include <span>
#include <utility>

namespace mobagen::compositions {
  namespace {

    bool descriptor_matches(const modules::LockedPluginActivationEntry& expected, const modules::ProviderDescriptor& actual) {
      if (actual.id != expected.provider_id || actual.version != expected.version
          || (!expected.configuration_schema.empty() && actual.configuration_schema != expected.configuration_schema)) {
        return false;
      }
      return std::ranges::all_of(expected.capabilities,
                                 [&](const auto& capability) { return std::ranges::find(actual.provides, capability) != actual.provides.end(); });
    }

    void append_shutdown_issues(NativeModuleManagerActionResult& result, std::string_view provider_id, plugins::NativePluginActionResult action) {
      if (action.ok()) return;
      result.issues.push_back({
          .code = NativeModuleManagerIssueCode::ShutdownFailed,
          .provider_id = std::string{provider_id},
          .message = "native module shutdown reported failures",
          .activation_issues = std::move(action.issues),
      });
    }

  }  // namespace

  NativeModuleManager::NativeModuleManager(std::unique_ptr<modules::LockedPluginActivationPlan> plan,
                                           std::vector<std::vector<std::byte>> configurations, plugins::PluginLogSink log_sink, void* log_context)
      : host_(log_sink, log_context), plan_(std::move(plan)), configurations_(std::move(configurations)) {
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

  NativeModuleManager::~NativeModuleManager() {
    for (auto activation = activation_order_.rbegin(); activation != activation_order_.rend(); ++activation) {
      activations_[*activation].reset();
    }
  }

  bool NativeModuleManager::activate_provider(std::size_t index, std::vector<std::size_t>& activated, NativeModuleManagerActionResult& result) {
    if (activations_[index] != nullptr) return true;
    for (const auto dependency : dependencies_[index]) {
      if (!activate_provider(dependency, activated, result)) return false;
    }

    const auto& entry = plan_->entries()[index];
    if (entry.linkage != modules::LinkageMode::Dynamic) {
      result.issues.push_back({
          .code = NativeModuleManagerIssueCode::UnsupportedLinkage,
          .provider_id = entry.provider_id,
          .message = "native module manager can activate only dynamic plugins",
      });
      return false;
    }
    if (!entry.binary_hash.empty()) {
      const auto hash = detail::hash_project_plugin_binary(entry.binary_path, modules::max_module_artifact_bytes);
      if (!hash.ok() || *hash.hash != entry.binary_hash) {
        result.issues.push_back({
            .code = NativeModuleManagerIssueCode::ArtifactVerificationFailed,
            .provider_id = entry.provider_id,
            .message
            = hash.ok() ? "selected native plugin no longer matches mobagen.lock" : "selected native plugin could not be verified: " + hash.error,
        });
        return false;
      }
    }
    auto loaded = plugins::load_native_plugin_binary(entry.binary_path, host_.api());
    if (!loaded.plugin.has_value()) {
      result.issues.push_back({
          .code = NativeModuleManagerIssueCode::LoadFailed,
          .provider_id = entry.provider_id,
          .message = "selected native plugin could not be loaded",
          .load_issues = std::move(loaded.issues),
      });
      return false;
    }
    if (!descriptor_matches(entry, loaded.plugin->contract().provider)) {
      result.issues.push_back({
          .code = NativeModuleManagerIssueCode::DescriptorMismatch,
          .provider_id = entry.provider_id,
          .message = "loaded plugin descriptor does not match the locked provider",
      });
      return false;
    }

    auto activation = plugins::activate_loaded_native_plugin(std::move(*loaded.plugin), host_, configurations_[index]);
    if (!activation.ok()) {
      result.issues.push_back({
          .code = NativeModuleManagerIssueCode::ActivationFailed,
          .provider_id = entry.provider_id,
          .message = "selected native plugin failed to activate",
          .activation_issues = std::move(activation.issues),
      });
      return false;
    }
    activations_[index] = std::move(activation.activation);
    activation_order_.push_back(index);
    activated.push_back(index);
    ++active_count_;
    return true;
  }

  void NativeModuleManager::rollback(std::vector<std::size_t>& activated, NativeModuleManagerActionResult& result) {
    for (auto index = activated.rbegin(); index != activated.rend(); ++index) {
      auto& activation = activations_[*index];
      if (activation != nullptr && activation->state() == plugins::NativePluginActivationState::Active) {
        append_shutdown_issues(result, activation->provider_id(), activation->quiesce());
      }
    }
    for (auto index = activated.rbegin(); index != activated.rend(); ++index) {
      auto& activation = activations_[*index];
      if (activation != nullptr && activation->state() == plugins::NativePluginActivationState::Quiesced) {
        append_shutdown_issues(result, activation->provider_id(), activation->stop());
      }
      if (activation != nullptr && activation->state() == plugins::NativePluginActivationState::Stopped) {
        activation.reset();
        --active_count_;
        const auto found = std::ranges::find(activation_order_, *index);
        if (found != activation_order_.end()) activation_order_.erase(found);
      }
    }
  }

  NativeModuleManagerActionResult NativeModuleManager::activate(std::string_view capability) {
    NativeModuleManagerActionResult result;
    if (!host_.owns_current_thread()) {
      result.issues.push_back({
          .code = NativeModuleManagerIssueCode::WrongThread,
          .message = "native modules must be activated on the manager owner thread",
      });
      return result;
    }
    const auto selected = capabilities_.find(capability);
    if (selected == capabilities_.end()) {
      result.issues.push_back({
          .code = NativeModuleManagerIssueCode::UnknownCapability,
          .message = "capability is not provided by the locked native module plan",
      });
      return result;
    }
    std::vector<std::size_t> activated;
    if (!activate_provider(selected->second, activated, result)) rollback(activated, result);
    return result;
  }

  NativeModuleCapabilityResult NativeModuleManager::acquire(std::string_view capability, std::uint32_t minimum_abi_version) {
    NativeModuleCapabilityResult result;
    auto activated = activate(capability);
    if (!activated.ok()) {
      result.issues = std::move(activated.issues);
      return result;
    }
    result.binding = host_.find_binding(capability, minimum_abi_version);
    if (!result.binding.has_value()) {
      const auto selected = capabilities_.find(capability);
      result.issues.push_back({
          .code = NativeModuleManagerIssueCode::UnsupportedCapabilityAbi,
          .provider_id = selected == capabilities_.end() ? std::string{} : plan_->entries()[selected->second].provider_id,
          .message = "native capability does not support the requested ABI version",
      });
    }
    return result;
  }

  NativeModuleManagerActionResult NativeModuleManager::stop() {
    NativeModuleManagerActionResult result;
    if (!host_.owns_current_thread()) {
      result.issues.push_back({
          .code = NativeModuleManagerIssueCode::WrongThread,
          .message = "native modules must be stopped on the manager owner thread",
      });
      return result;
    }
    for (auto index = activation_order_.rbegin(); index != activation_order_.rend(); ++index) {
      auto& activation = activations_[*index];
      if (activation->state() == plugins::NativePluginActivationState::Active) {
        append_shutdown_issues(result, activation->provider_id(), activation->quiesce());
      }
    }
    for (auto index = activation_order_.rbegin(); index != activation_order_.rend(); ++index) {
      auto& activation = activations_[*index];
      if (activation->state() == plugins::NativePluginActivationState::Quiesced) {
        append_shutdown_issues(result, activation->provider_id(), activation->stop());
      }
    }
    std::erase_if(activation_order_, [&](const auto index) {
      auto& activation = activations_[index];
      if (activation->state() != plugins::NativePluginActivationState::Stopped) return false;
      activation.reset();
      --active_count_;
      return true;
    });
    return result;
  }

  NativeModuleManagerCreateResult create_native_module_manager(std::unique_ptr<modules::LockedPluginActivationPlan> plan,
                                                               std::span<const NativeModuleConfiguration> configurations,
                                                               plugins::PluginLogSink log_sink, void* log_context) {
    NativeModuleManagerCreateResult result;
    if (plan == nullptr) {
      result.issues.push_back({
          .code = NativeModuleManagerIssueCode::InvalidPlan,
          .message = "native module manager requires a locked activation plan",
      });
      return result;
    }
    std::map<std::string, std::span<const std::byte>, std::less<>> supplied;
    for (const auto& configuration : configurations) {
      if (!supplied.emplace(configuration.provider_id, std::span<const std::byte>{configuration.data}).second) {
        result.issues.push_back({
            .code = NativeModuleManagerIssueCode::InvalidConfiguration,
            .provider_id = configuration.provider_id,
            .message = "native module configuration providers must be unique",
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
              .code = NativeModuleManagerIssueCode::InvalidConfiguration,
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
            .code = NativeModuleManagerIssueCode::InvalidConfiguration,
            .provider_id = entry.provider_id,
            .message = "locked plugin configuration bytes are missing",
        });
        return result;
      }
      const auto digest = assets::sha256(configuration->second);
      if (!digest.has_value() || assets::to_string(*digest) != entry.configuration_hash) {
        result.issues.push_back({
            .code = NativeModuleManagerIssueCode::InvalidConfiguration,
            .provider_id = entry.provider_id,
            .message = "native module configuration does not match mobagen.lock",
        });
        return result;
      }
      owned_configurations.emplace_back(configuration->second.begin(), configuration->second.end());
      supplied.erase(configuration);
    }
    if (!supplied.empty()) {
      result.issues.push_back({
          .code = NativeModuleManagerIssueCode::InvalidConfiguration,
          .provider_id = supplied.begin()->first,
          .message = "configuration provider is not part of the native activation plan",
      });
      return result;
    }
    result.manager
        = std::unique_ptr<NativeModuleManager>(new NativeModuleManager(std::move(plan), std::move(owned_configurations), log_sink, log_context));
    return result;
  }

}  // namespace mobagen::compositions
