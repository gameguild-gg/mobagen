#pragma once

#include "modules/descriptor.hpp"
#include "wasm_plugin_contract.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

}  // namespace mobagen::plugins
