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
#include <utility>
#include <vector>

namespace mobagen::modules {
  class CapabilityRegistry;
}

namespace mobagen::plugins {

  class WasmCommandChannel;
  class WasmHostImports;
  struct WasmCommandChannelOpenResult;
  class LoadedPortableWasmPlugin;

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
    std::optional<std::string> error;

    [[nodiscard]] bool ok() const noexcept { return value.has_value() && !error.has_value(); }
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
    [[nodiscard]] WasmHostImports* host_imports() noexcept { return host_imports_.get(); }
    [[nodiscard]] const WasmHostImports* host_imports() const noexcept { return host_imports_.get(); }

  protected:
    explicit PortableWasmInstance(std::shared_ptr<WasmHostImports> host_imports) : host_imports_(std::move(host_imports)) {}

  private:
    std::shared_ptr<WasmHostImports> host_imports_;
  };

  [[nodiscard]] WasmInvocationResult invoke_portable_wasm(PortableWasmInstance& instance, WasmPluginExport function,
                                                          std::span<const std::uint32_t> arguments);

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
    CommandChannelCloseFailed,
    InvalidTransition,
    WrongThread,
    OutOfMemory,
    HostImportsFailed,
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
    [[nodiscard]] std::size_t command_channel_count() const noexcept { return command_channels_.size(); }
    [[nodiscard]] WasmCommandChannelOpenResult open_command_channel(std::uint32_t input_capacity, std::uint32_t output_capacity);
    [[nodiscard]] PortableWasmPluginActionResult quiesce();
    [[nodiscard]] PortableWasmPluginActionResult stop();

  private:
    friend PortableWasmPluginActivationResult activate_portable_wasm_plugin(std::unique_ptr<PortableWasmInstance>, std::span<const std::byte>);
    friend PortableWasmPluginActivationResult activate_loaded_portable_wasm_plugin(LoadedPortableWasmPlugin, std::span<const std::byte>);
    friend PortableWasmPluginActivationResult activate_loaded_portable_wasm_plugin(LoadedPortableWasmPlugin,
                                                                                    std::shared_ptr<const modules::CapabilityRegistry>,
                                                                                    std::span<const std::byte>);

    PortableWasmPluginActivation(std::unique_ptr<PortableWasmInstance> instance, modules::ProviderDescriptor provider);
    [[nodiscard]] static PortableWasmPluginActivationResult activate_queried(
        std::unique_ptr<PortableWasmInstance>, modules::ProviderDescriptor, std::shared_ptr<const modules::CapabilityRegistry>, bool,
        std::span<const std::byte>);
    [[nodiscard]] PortableWasmPluginActionResult start();
    void close_command_channels(PortableWasmPluginActionResult& result);
    void shutdown_noexcept() noexcept;

    std::unique_ptr<PortableWasmInstance> instance_;
    modules::ProviderDescriptor provider_;
    std::thread::id owner_thread_;
    PortableWasmPluginState state_{PortableWasmPluginState::Configured};
    std::vector<std::unique_ptr<WasmCommandChannel>> command_channels_;
  };

  struct PortableWasmPluginActivationResult {
    std::unique_ptr<PortableWasmPluginActivation> activation;
    std::vector<PortableWasmPluginIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return activation != nullptr && issues.empty(); }
  };

  [[nodiscard]] PortableWasmPluginActivationResult activate_portable_wasm_plugin(std::unique_ptr<PortableWasmInstance> instance,
                                                                                 std::span<const std::byte> configuration = {});

}  // namespace mobagen::plugins
