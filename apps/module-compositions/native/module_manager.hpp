#pragma once

#include "modules/locked_activation_plan.hpp"
#include "plugins/plugin_activation.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mobagen::compositions {

  enum class NativeModuleManagerIssueCode : std::uint8_t {
    InvalidPlan,
    InvalidConfiguration,
    WrongThread,
    UnknownCapability,
    UnsupportedLinkage,
    ArtifactVerificationFailed,
    LoadFailed,
    DescriptorMismatch,
    ActivationFailed,
    UnsupportedCapabilityAbi,
    ShutdownFailed,
  };

  struct NativeModuleManagerIssue {
    NativeModuleManagerIssueCode code{};
    std::string provider_id;
    std::string message;
    std::vector<plugins::NativePluginLoadIssue> load_issues;
    std::vector<plugins::NativePluginActivationIssue> activation_issues;
  };

  struct NativeModuleManagerActionResult {
    std::vector<NativeModuleManagerIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  struct NativeModuleCapabilityResult {
    std::optional<plugins::NativeCapabilityBindingView> binding;
    std::vector<NativeModuleManagerIssue> issues;

    [[nodiscard]] bool ok() const noexcept {
      return binding.has_value() && issues.empty();
    }
  };

  struct NativeModuleConfiguration {
    std::string provider_id;
    std::vector<std::byte> data;
  };

  class NativeModuleManager;

  struct NativeModuleManagerCreateResult {
    std::unique_ptr<NativeModuleManager> manager;
    std::vector<NativeModuleManagerIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return manager != nullptr && issues.empty(); }
  };

  class NativeModuleManager {
  public:
    NativeModuleManager(const NativeModuleManager&) = delete;
    NativeModuleManager& operator=(const NativeModuleManager&) = delete;
    NativeModuleManager(NativeModuleManager&&) = delete;
    NativeModuleManager& operator=(NativeModuleManager&&) = delete;
    ~NativeModuleManager();

    [[nodiscard]] NativeModuleManagerActionResult activate(std::string_view capability);
    [[nodiscard]] NativeModuleCapabilityResult acquire(
        std::string_view capability, std::uint32_t minimum_abi_version
    );
    [[nodiscard]] NativeModuleManagerActionResult stop();
    [[nodiscard]] std::size_t active_count() const noexcept { return active_count_; }
    [[nodiscard]] plugins::PluginHost& host() noexcept { return host_; }
    [[nodiscard]] const plugins::PluginHost& host() const noexcept { return host_; }

  private:
    friend NativeModuleManagerCreateResult create_native_module_manager(
        std::unique_ptr<modules::LockedPluginActivationPlan>,
        std::span<const NativeModuleConfiguration>, plugins::PluginLogSink, void*
    );

    NativeModuleManager(
        std::unique_ptr<modules::LockedPluginActivationPlan> plan,
        std::vector<std::vector<std::byte>> configurations,
        plugins::PluginLogSink log_sink, void* log_context
    );
    bool activate_provider(
        std::size_t index, std::vector<std::size_t>& activated,
        NativeModuleManagerActionResult& result
    );
    void rollback(
        std::vector<std::size_t>& activated, NativeModuleManagerActionResult& result
    );

    plugins::PluginHost host_;
    std::unique_ptr<modules::LockedPluginActivationPlan> plan_;
    std::map<std::string, std::size_t, std::less<>> providers_;
    std::map<std::string, std::size_t, std::less<>> capabilities_;
    std::vector<std::vector<std::size_t>> dependencies_;
    std::vector<std::vector<std::byte>> configurations_;
    std::vector<std::unique_ptr<plugins::NativePluginActivation>> activations_;
    std::vector<std::size_t> activation_order_;
    std::size_t active_count_{};
  };

  /* Construction indexes metadata only; native code is loaded by activate().
     The manager must be activated, stopped, and destroyed on its construction thread. */
  [[nodiscard]] NativeModuleManagerCreateResult create_native_module_manager(
      std::unique_ptr<modules::LockedPluginActivationPlan> plan,
      std::span<const NativeModuleConfiguration> configurations = {},
      plugins::PluginLogSink log_sink = nullptr, void* log_context = nullptr
  );

}  // namespace mobagen::compositions
