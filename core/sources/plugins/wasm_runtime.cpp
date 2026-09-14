#include "wasm_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <span>
#include <string>
#include <utility>

namespace mobagen::plugins {
  namespace {

    void add_issue(WasmPluginQueryResult& result, WasmPluginQueryIssueCode code, WasmPluginExport phase, std::string message,
                   std::uint32_t status = MOBAGEN_WASM_STATUS_OK, std::vector<WasmPluginContractIssue> contract_issues = {}) {
      result.issues.push_back({code, phase, status, std::move(message), std::move(contract_issues)});
    }

    WasmInvocationResult invoke_safely(PortableWasmInstance& instance, WasmPluginExport function, std::span<const std::uint32_t> arguments) {
      try {
        return instance.invoke(function, arguments);
      } catch (const std::exception& exception) {
        return WasmInvocationResult::failure(std::string{"WASM backend invocation threw: "} + exception.what());
      } catch (...) {
        return WasmInvocationResult::failure("WASM backend invocation threw");
      }
    }

    bool exchange_is_valid(std::span<const std::byte> memory, std::uint32_t offset, std::uint32_t size) noexcept {
      return offset != MOBAGEN_WASM_NULL_OFFSET && offset % MOBAGEN_WASM_EXCHANGE_ALIGNMENT == 0 && static_cast<std::size_t>(offset) <= memory.size()
             && static_cast<std::size_t>(size) <= memory.size() - static_cast<std::size_t>(offset);
    }

    void release_exchange(PortableWasmInstance& instance, std::uint32_t offset, std::uint32_t size, WasmPluginQueryResult& result) {
      const std::array arguments{offset, size, MOBAGEN_WASM_EXCHANGE_ALIGNMENT};
      auto released = invoke_safely(instance, WasmPluginExport::Deallocate, arguments);
      if (!released.ok()) {
        add_issue(result, WasmPluginQueryIssueCode::DeallocationFailed, WasmPluginExport::Deallocate,
                  released.error.empty() ? "WASM guest deallocation failed" : std::move(released.error), MOBAGEN_WASM_STATUS_FAILED);
        return;
      }
      if (*released.value != MOBAGEN_WASM_STATUS_OK) {
        add_issue(result, WasmPluginQueryIssueCode::DeallocationFailed, WasmPluginExport::Deallocate,
                  "WASM guest deallocation callback reported failure", *released.value);
      }
    }

  }  // namespace

  std::string_view wasm_plugin_export_name(WasmPluginExport function) noexcept {
    switch (function) {
      case WasmPluginExport::Allocate:
        return MOBAGEN_WASM_EXPORT_ALLOCATE_V1;
      case WasmPluginExport::Deallocate:
        return MOBAGEN_WASM_EXPORT_DEALLOCATE_V1;
      case WasmPluginExport::Query:
        return MOBAGEN_WASM_EXPORT_QUERY_V1;
      case WasmPluginExport::Configure:
        return MOBAGEN_WASM_EXPORT_CONFIGURE_V1;
      case WasmPluginExport::Start:
        return MOBAGEN_WASM_EXPORT_START_V1;
      case WasmPluginExport::Quiesce:
        return MOBAGEN_WASM_EXPORT_QUIESCE_V1;
      case WasmPluginExport::Stop:
        return MOBAGEN_WASM_EXPORT_STOP_V1;
      case WasmPluginExport::Process:
        return MOBAGEN_WASM_EXPORT_PROCESS_V1;
    }
    return {};
  }

  WasmInvocationResult WasmInvocationResult::success(std::uint32_t value) { return {value, {}}; }

  WasmInvocationResult WasmInvocationResult::failure(std::string error) { return {std::nullopt, std::move(error)}; }

  WasmPluginQueryResult query_portable_wasm_plugin(PortableWasmInstance& instance) {
    WasmPluginQueryResult result;
    constexpr std::uint32_t descriptor_size = MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE;
    const std::array allocate_arguments{descriptor_size, MOBAGEN_WASM_EXCHANGE_ALIGNMENT};
    auto allocated = invoke_safely(instance, WasmPluginExport::Allocate, allocate_arguments);
    if (!allocated.ok()) {
      add_issue(result, WasmPluginQueryIssueCode::BackendFailure, WasmPluginExport::Allocate,
                allocated.error.empty() ? "WASM guest allocation failed" : std::move(allocated.error), MOBAGEN_WASM_STATUS_FAILED);
      return result;
    }

    const auto descriptor_offset = *allocated.value;
    if (descriptor_offset == MOBAGEN_WASM_NULL_OFFSET) {
      add_issue(result, WasmPluginQueryIssueCode::AllocationFailed, WasmPluginExport::Allocate,
                "WASM guest could not allocate descriptor exchange memory", MOBAGEN_WASM_STATUS_OUT_OF_MEMORY);
      return result;
    }

    auto exchange_memory = instance.writable_memory();
    if (!exchange_is_valid(exchange_memory, descriptor_offset, descriptor_size)) {
      add_issue(result, WasmPluginQueryIssueCode::InvalidExchangeBuffer, WasmPluginExport::Allocate,
                "WASM guest allocator returned a misaligned or out-of-bounds descriptor buffer", MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
      release_exchange(instance, descriptor_offset, descriptor_size, result);
      return result;
    }
    std::ranges::fill(exchange_memory.subspan(descriptor_offset, descriptor_size), std::byte{});

    const std::array query_arguments{descriptor_offset, descriptor_size};
    auto queried = invoke_safely(instance, WasmPluginExport::Query, query_arguments);
    if (!queried.ok()) {
      add_issue(result, WasmPluginQueryIssueCode::BackendFailure, WasmPluginExport::Query,
                queried.error.empty() ? "WASM plugin query failed" : std::move(queried.error), MOBAGEN_WASM_STATUS_FAILED);
      release_exchange(instance, descriptor_offset, descriptor_size, result);
      return result;
    }
    if (*queried.value != MOBAGEN_WASM_STATUS_OK) {
      add_issue(result, WasmPluginQueryIssueCode::CallbackFailed, WasmPluginExport::Query, "WASM plugin query callback reported failure",
                *queried.value);
      release_exchange(instance, descriptor_offset, descriptor_size, result);
      return result;
    }

    std::optional<modules::ProviderDescriptor> provider;
    try {
      auto decoded = decode_wasm_plugin_descriptor(instance.memory(), descriptor_offset);
      if (!decoded.ok()) {
        add_issue(result, WasmPluginQueryIssueCode::ContractInvalid, WasmPluginExport::Query, "WASM plugin descriptor is invalid",
                  MOBAGEN_WASM_STATUS_INVALID_ARGUMENT, std::move(decoded.issues));
      } else {
        provider = std::move(decoded.provider);
      }
    } catch (const std::bad_alloc&) {
      add_issue(result, WasmPluginQueryIssueCode::OutOfMemory, WasmPluginExport::Query, "WASM plugin descriptor decoding ran out of memory",
                MOBAGEN_WASM_STATUS_OUT_OF_MEMORY);
    } catch (const std::exception& exception) {
      add_issue(result, WasmPluginQueryIssueCode::ContractInvalid, WasmPluginExport::Query,
                std::string{"WASM plugin descriptor decoding failed: "} + exception.what(), MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
    } catch (...) {
      add_issue(result, WasmPluginQueryIssueCode::ContractInvalid, WasmPluginExport::Query, "WASM plugin descriptor decoding failed",
                MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
    }

    release_exchange(instance, descriptor_offset, descriptor_size, result);
    if (result.issues.empty()) result.provider = std::move(provider);
    return result;
  }

}  // namespace mobagen::plugins
