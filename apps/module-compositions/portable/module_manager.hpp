#pragma once

#include "modules/locked_activation_plan.hpp"
#include "plugins/wasm_plugin_loader.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace mobagen::compositions {

  enum class PortableModuleManagerIssueCode : std::uint8_t {
    InvalidPlan,
    InvalidConfiguration,
    InvalidPermissionGrant,
    WrongThread,
    UnknownCapability,
    UnsupportedLinkage,
    ArtifactVerificationFailed,
    LoadFailed,
    DescriptorMismatch,
    PermissionDenied,
    RegistryFailed,
    ActivationFailed,
    CapabilityUnavailable,
    ShutdownFailed,
  };

  struct PortableModuleManagerIssue {
    PortableModuleManagerIssueCode code{};
    std::string provider_id;
    std::string message;
    std::vector<modules::RegistryIssue> registry_issues;
    std::vector<plugins::PortableWasmPluginLoadIssue> load_issues;
    std::vector<plugins::PortableWasmPluginIssue> activation_issues;
  };

  struct PortableModuleManagerActionResult {
    std::vector<PortableModuleManagerIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  struct PortableModuleCapabilityResult {
    plugins::PortableWasmPluginActivation* plugin{};
    std::vector<PortableModuleManagerIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return plugin != nullptr && issues.empty(); }
  };

  struct PortableModuleConfiguration {
    std::string provider_id;
    std::vector<std::byte> data;
  };

  class PortableModuleManager;

  struct PortableModuleManagerCreateResult {
    std::unique_ptr<PortableModuleManager> manager;
    std::vector<PortableModuleManagerIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return manager != nullptr && issues.empty(); }
  };

  class PortableModuleManager {
  public:
    PortableModuleManager(const PortableModuleManager&) = delete;
    PortableModuleManager& operator=(const PortableModuleManager&) = delete;
    PortableModuleManager(PortableModuleManager&&) = delete;
    PortableModuleManager& operator=(PortableModuleManager&&) = delete;
    ~PortableModuleManager();

    [[nodiscard]] PortableModuleManagerActionResult activate(std::string_view capability);
    [[nodiscard]] PortableModuleCapabilityResult acquire(std::string_view capability);
    [[nodiscard]] PortableModuleManagerActionResult stop();
    [[nodiscard]] std::size_t active_count() const noexcept { return active_count_; }
    [[nodiscard]] plugins::PortableWasmPluginActivation* plugin(std::string_view provider_id) noexcept;
    [[nodiscard]] const plugins::PortableWasmPluginActivation* plugin(std::string_view provider_id) const noexcept;

  private:
    friend PortableModuleManagerCreateResult create_portable_module_manager(std::unique_ptr<modules::LockedPluginActivationPlan>,
                                                                            plugins::PortableWasmBackend&,
                                                                            std::span<const PortableModuleConfiguration>,
                                                                            std::span<const modules::ProviderDescriptor>,
                                                                            std::span<const std::string>, plugins::WasmHostServices);

    PortableModuleManager(std::unique_ptr<modules::LockedPluginActivationPlan> plan, plugins::PortableWasmBackend& backend,
                          std::vector<std::vector<std::byte>> configurations, std::vector<modules::ProviderDescriptor> builtin_providers,
                          std::vector<std::string> granted_permissions, plugins::WasmHostServices host_services);
    void collect_inactive_closure(std::size_t index, std::vector<bool>& visited, std::vector<std::size_t>& closure) const;
    void rollback(std::vector<std::size_t>& activated, PortableModuleManagerActionResult& result);

    std::unique_ptr<modules::LockedPluginActivationPlan> plan_;
    plugins::PortableWasmBackend* backend_{};
    plugins::WasmHostServices host_services_;
    std::thread::id owner_thread_;
    std::map<std::string, std::size_t, std::less<>> providers_;
    std::map<std::string, std::size_t, std::less<>> capabilities_;
    std::vector<std::vector<std::size_t>> dependencies_;
    std::vector<std::vector<std::byte>> configurations_;
    std::vector<modules::ProviderDescriptor> builtin_providers_;
    std::vector<std::string> granted_permissions_;
    std::vector<std::unique_ptr<plugins::PortableWasmPluginActivation>> activations_;
    std::vector<std::size_t> activation_order_;
    std::size_t active_count_{};
  };

  /* Construction indexes lock metadata only. WASM bytes are verified, instantiated,
     queried, and activated only for the first requested capability closure.
     The backend, host_services.state, and manager lifecycle must remain on the
     construction thread for the manager lifetime. */
  [[nodiscard]] PortableModuleManagerCreateResult create_portable_module_manager(std::unique_ptr<modules::LockedPluginActivationPlan> plan,
                                                                                 plugins::PortableWasmBackend& backend,
                                                                                 std::span<const PortableModuleConfiguration> configurations = {},
                                                                                 std::span<const modules::ProviderDescriptor> builtin_providers = {},
                                                                                 std::span<const std::string> granted_permissions = {},
                                                                                 plugins::WasmHostServices host_services = {});

}  // namespace mobagen::compositions
