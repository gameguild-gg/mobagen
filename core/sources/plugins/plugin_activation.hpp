#pragma once

#include "plugin_host.hpp"
#include "plugin_loader.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mobagen::plugins {

  enum class NativePluginActivationState : std::uint8_t { Configured, Active, Quiesced, Stopped };
  enum class NativePluginLifecyclePhase : std::uint8_t { Load, Register, Configure, Start, Quiesce, Stop };
  enum class NativePluginActivationIssueCode : std::uint8_t {
    LoadFailed,
    RegistrationFailed,
    CallbackFailed,
    CallbackException,
    CapabilityMismatch,
    InvalidTransition,
    WrongThread,
    OutOfMemory,
  };

  struct NativePluginActivationIssue {
    NativePluginActivationIssueCode code{};
    NativePluginLifecyclePhase phase{};
    MobagenStatus status{MOBAGEN_STATUS_OK};
    std::string message;
    std::vector<NativePluginLoadIssue> load_issues;
  };

  struct NativePluginActionResult {
    std::vector<NativePluginActivationIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  struct NativePluginActivationResult;

  [[nodiscard]] NativePluginActivationResult activate_loaded_native_plugin(NativePlugin plugin, PluginHost& host,
                                                                           std::span<const std::byte> configuration = {});

  class NativePluginActivation {
  public:
    NativePluginActivation(const NativePluginActivation&) = delete;
    NativePluginActivation& operator=(const NativePluginActivation&) = delete;
    NativePluginActivation(NativePluginActivation&&) = delete;
    NativePluginActivation& operator=(NativePluginActivation&&) = delete;
    ~NativePluginActivation();

    [[nodiscard]] NativePluginActivationState state() const noexcept { return state_; }
    [[nodiscard]] std::string_view provider_id() const noexcept { return provider_id_; }
    [[nodiscard]] NativePluginActionResult quiesce();
    [[nodiscard]] NativePluginActionResult stop();

  private:
    friend NativePluginActivationResult activate_native_plugin_package(const std::filesystem::path&, PluginHost&, std::span<const std::byte>);
    friend NativePluginActivationResult activate_loaded_native_plugin(NativePlugin, PluginHost&, std::span<const std::byte>);

    NativePluginActivation(PluginHost& host, std::string provider_id, NativePlugin plugin) noexcept;
    [[nodiscard]] NativePluginActionResult start();
    void cleanup_failed_start(NativePluginActionResult& result);
    void shutdown_noexcept() noexcept;

    PluginHost& host_;
    std::string provider_id_;
    NativePlugin plugin_;
    NativePluginActivationState state_{NativePluginActivationState::Configured};
  };

  struct NativePluginActivationResult {
    std::unique_ptr<NativePluginActivation> activation;
    std::vector<NativePluginActivationIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return activation != nullptr && issues.empty(); }
  };

  [[nodiscard]] NativePluginActivationResult activate_native_plugin_package(const std::filesystem::path& package, PluginHost& host,
                                                                            std::span<const std::byte> configuration = {});

}  // namespace mobagen::plugins
