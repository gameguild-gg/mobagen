#pragma once

#include "modules/descriptor.hpp"
#include "wasm_plugin_contract.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace mobagen::plugins {

  enum class WasmPluginExport : std::uint8_t {
    Allocate,
    Deallocate,
    Query,
    Configure,
    Start,
    Quiesce,
    Stop,
    Process,
  };

  [[nodiscard]] std::string_view wasm_plugin_export_name(WasmPluginExport function) noexcept;

  struct WasmInvocationResult {
    std::optional<std::uint32_t> value;
    std::string error;

    [[nodiscard]] bool ok() const noexcept { return value.has_value() && error.empty(); }
    [[nodiscard]] static WasmInvocationResult success(std::uint32_t value);
    [[nodiscard]] static WasmInvocationResult failure(std::string error);
  };

  class PortableWasmInstance {
  public:
    PortableWasmInstance() = default;
    PortableWasmInstance(const PortableWasmInstance&) = delete;
    PortableWasmInstance& operator=(const PortableWasmInstance&) = delete;
    PortableWasmInstance(PortableWasmInstance&&) = delete;
    PortableWasmInstance& operator=(PortableWasmInstance&&) = delete;
    virtual ~PortableWasmInstance() = default;

    [[nodiscard]] virtual WasmInvocationResult invoke(WasmPluginExport function, std::span<const std::uint32_t> arguments) = 0;

    /* Memory views remain valid only until the next invoke call. */
    [[nodiscard]] virtual std::span<const std::byte> memory() const noexcept = 0;
    [[nodiscard]] virtual std::span<std::byte> writable_memory() noexcept = 0;
  };

  enum class WasmPluginQueryIssueCode : std::uint8_t {
    BackendFailure,
    AllocationFailed,
    InvalidExchangeBuffer,
    CallbackFailed,
    ContractInvalid,
    DeallocationFailed,
    OutOfMemory,
  };

  struct WasmPluginQueryIssue {
    WasmPluginQueryIssueCode code{};
    WasmPluginExport phase{};
    std::uint32_t status{MOBAGEN_WASM_STATUS_OK};
    std::string message;
    std::vector<WasmPluginContractIssue> contract_issues;
  };

  struct WasmPluginQueryResult {
    std::optional<modules::ProviderDescriptor> provider;
    std::vector<WasmPluginQueryIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return provider.has_value() && issues.empty(); }
  };

  [[nodiscard]] WasmPluginQueryResult query_portable_wasm_plugin(PortableWasmInstance& instance);

  enum class PortableWasmPluginState : std::uint8_t { Configured, Active, Quiesced, Stopped };

  enum class PortableWasmPluginIssueCode : std::uint8_t {
    InvalidInstance,
    QueryFailed,
    ConfigurationTooLarge,
    BackendFailure,
    AllocationFailed,
    InvalidExchangeBuffer,
    CallbackFailed,
    DeallocationFailed,
    InvalidTransition,
    WrongThread,
    OutOfMemory,
  };

  struct PortableWasmPluginIssue {
    PortableWasmPluginIssueCode code{};
    WasmPluginExport phase{};
    std::uint32_t status{MOBAGEN_WASM_STATUS_OK};
    std::string message;
    std::vector<WasmPluginQueryIssue> query_issues;
  };

  struct PortableWasmPluginActionResult {
    std::vector<PortableWasmPluginIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  struct PortableWasmPluginActivationResult;

  class PortableWasmPluginActivation {
  public:
    PortableWasmPluginActivation(const PortableWasmPluginActivation&) = delete;
    PortableWasmPluginActivation& operator=(const PortableWasmPluginActivation&) = delete;
    PortableWasmPluginActivation(PortableWasmPluginActivation&&) = delete;
    PortableWasmPluginActivation& operator=(PortableWasmPluginActivation&&) = delete;
    ~PortableWasmPluginActivation();

    [[nodiscard]] PortableWasmPluginState state() const noexcept { return state_; }
    [[nodiscard]] const modules::ProviderDescriptor& provider() const noexcept { return provider_; }
    [[nodiscard]] PortableWasmPluginActionResult quiesce();
    [[nodiscard]] PortableWasmPluginActionResult stop();

  private:
    friend PortableWasmPluginActivationResult activate_portable_wasm_plugin(std::unique_ptr<PortableWasmInstance>, std::span<const std::byte>);

    PortableWasmPluginActivation(std::unique_ptr<PortableWasmInstance> instance, modules::ProviderDescriptor provider);
    [[nodiscard]] PortableWasmPluginActionResult start();
    void shutdown_noexcept() noexcept;

    std::unique_ptr<PortableWasmInstance> instance_;
    modules::ProviderDescriptor provider_;
    std::thread::id owner_thread_;
    PortableWasmPluginState state_{PortableWasmPluginState::Configured};
  };

  struct PortableWasmPluginActivationResult {
    std::unique_ptr<PortableWasmPluginActivation> activation;
    std::vector<PortableWasmPluginIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return activation != nullptr && issues.empty(); }
  };

  [[nodiscard]] PortableWasmPluginActivationResult activate_portable_wasm_plugin(std::unique_ptr<PortableWasmInstance> instance,
                                                                                 std::span<const std::byte> configuration = {});

}  // namespace mobagen::plugins
