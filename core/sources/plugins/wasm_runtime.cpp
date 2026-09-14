#include "wasm_runtime.hpp"

#include "wasm_command_channel.hpp"
#include "wasm_plugin_loader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <span>
#include <string>
#include <thread>
#include <utility>

namespace mobagen::plugins {
  namespace {

    void add_issue(WasmPluginQueryResult& result, WasmPluginQueryIssueCode code, WasmPluginExport phase, std::string message,
                   std::uint32_t status = MOBAGEN_WASM_STATUS_OK, std::vector<WasmPluginContractIssue> contract_issues = {}) {
      result.issues.push_back({code, phase, status, std::move(message), std::move(contract_issues)});
    }

    bool exchange_is_valid(std::span<const std::byte> memory, std::uint32_t offset, std::uint32_t size) noexcept {
      return offset != MOBAGEN_WASM_NULL_OFFSET && offset % MOBAGEN_WASM_EXCHANGE_ALIGNMENT == 0 && static_cast<std::size_t>(offset) <= memory.size()
             && static_cast<std::size_t>(size) <= memory.size() - static_cast<std::size_t>(offset);
    }

    void release_exchange(PortableWasmInstance& instance, std::uint32_t offset, std::uint32_t size, WasmPluginQueryResult& result) {
      const std::array arguments{offset, size, MOBAGEN_WASM_EXCHANGE_ALIGNMENT};
      auto released = invoke_portable_wasm(instance, WasmPluginExport::Deallocate, arguments);
      if (!released.ok()) {
        add_issue(result, WasmPluginQueryIssueCode::DeallocationFailed, WasmPluginExport::Deallocate,
                  released.error.has_value() ? std::move(*released.error) : "WASM guest deallocation failed", MOBAGEN_WASM_STATUS_FAILED);
        return;
      }
      if (*released.value != MOBAGEN_WASM_STATUS_OK) {
        add_issue(result, WasmPluginQueryIssueCode::DeallocationFailed, WasmPluginExport::Deallocate,
                  "WASM guest deallocation callback reported failure", *released.value);
      }
    }

    void add_activation_issue(PortableWasmPluginActionResult& result, PortableWasmPluginIssueCode code, WasmPluginExport phase, std::string message,
                              std::uint32_t status = MOBAGEN_WASM_STATUS_OK, std::vector<WasmPluginQueryIssue> query_issues = {}) {
      result.issues.push_back({code, phase, status, std::move(message), std::move(query_issues)});
    }

    void release_activation_exchange(PortableWasmInstance& instance, std::uint32_t offset, std::uint32_t size,
                                     PortableWasmPluginActionResult& result) {
      const std::array arguments{offset, size, MOBAGEN_WASM_EXCHANGE_ALIGNMENT};
      auto released = invoke_portable_wasm(instance, WasmPluginExport::Deallocate, arguments);
      if (!released.ok()) {
        add_activation_issue(result, PortableWasmPluginIssueCode::DeallocationFailed, WasmPluginExport::Deallocate,
                             released.error.has_value() ? std::move(*released.error) : "WASM guest deallocation failed", MOBAGEN_WASM_STATUS_FAILED);
      } else if (*released.value != MOBAGEN_WASM_STATUS_OK) {
        add_activation_issue(result, PortableWasmPluginIssueCode::DeallocationFailed, WasmPluginExport::Deallocate,
                             "WASM guest deallocation callback reported failure", *released.value);
      }
    }

    void invoke_lifecycle(PortableWasmInstance& instance, WasmPluginExport phase, PortableWasmPluginActionResult& result) {
      auto invoked = invoke_portable_wasm(instance, phase, {});
      if (!invoked.ok()) {
        add_activation_issue(result, PortableWasmPluginIssueCode::BackendFailure, phase,
                             invoked.error.has_value() ? std::move(*invoked.error) : "WASM plugin lifecycle invocation failed",
                             MOBAGEN_WASM_STATUS_FAILED);
      } else if (*invoked.value != MOBAGEN_WASM_STATUS_OK) {
        add_activation_issue(result, PortableWasmPluginIssueCode::CallbackFailed, phase, "WASM plugin lifecycle callback reported failure",
                             *invoked.value);
      }
    }

    PortableWasmPluginActionResult configure_instance(PortableWasmInstance& instance, std::span<const std::byte> configuration) {
      PortableWasmPluginActionResult result;
      if (configuration.size() > MOBAGEN_WASM_MAX_CONFIGURATION_BYTES) {
        add_activation_issue(result, PortableWasmPluginIssueCode::ConfigurationTooLarge, WasmPluginExport::Configure,
                             "WASM plugin configuration exceeds the 1 MiB limit", MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
        return result;
      }

      std::uint32_t configuration_offset = MOBAGEN_WASM_NULL_OFFSET;
      const auto configuration_size = static_cast<std::uint32_t>(configuration.size());
      if (!configuration.empty()) {
        const std::array allocate_arguments{configuration_size, MOBAGEN_WASM_EXCHANGE_ALIGNMENT};
        auto allocated = invoke_portable_wasm(instance, WasmPluginExport::Allocate, allocate_arguments);
        if (!allocated.ok()) {
          add_activation_issue(result, PortableWasmPluginIssueCode::BackendFailure, WasmPluginExport::Allocate,
                               allocated.error.has_value() ? std::move(*allocated.error) : "WASM guest configuration allocation failed",
                               MOBAGEN_WASM_STATUS_FAILED);
          return result;
        }
        configuration_offset = *allocated.value;
        if (configuration_offset == MOBAGEN_WASM_NULL_OFFSET) {
          add_activation_issue(result, PortableWasmPluginIssueCode::AllocationFailed, WasmPluginExport::Allocate,
                               "WASM guest could not allocate configuration exchange memory", MOBAGEN_WASM_STATUS_OUT_OF_MEMORY);
          return result;
        }

        auto memory = instance.writable_memory();
        if (!exchange_is_valid(memory, configuration_offset, configuration_size)) {
          add_activation_issue(result, PortableWasmPluginIssueCode::InvalidExchangeBuffer, WasmPluginExport::Allocate,
                               "WASM guest allocator returned a misaligned or out-of-bounds configuration buffer",
                               MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
          release_activation_exchange(instance, configuration_offset, configuration_size, result);
          return result;
        }
        std::ranges::copy(configuration, memory.begin() + configuration_offset);
      }

      const std::array configure_arguments{configuration_offset, configuration_size};
      auto configured = invoke_portable_wasm(instance, WasmPluginExport::Configure, configure_arguments);
      if (!configured.ok()) {
        add_activation_issue(result, PortableWasmPluginIssueCode::BackendFailure, WasmPluginExport::Configure,
                             configured.error.has_value() ? std::move(*configured.error) : "WASM plugin configure invocation failed",
                             MOBAGEN_WASM_STATUS_FAILED);
      } else if (*configured.value != MOBAGEN_WASM_STATUS_OK) {
        add_activation_issue(result, PortableWasmPluginIssueCode::CallbackFailed, WasmPluginExport::Configure,
                             "WASM plugin configure callback reported failure", *configured.value);
      }
      if (!configuration.empty()) release_activation_exchange(instance, configuration_offset, configuration_size, result);
      return result;
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

  WasmInvocationResult WasmInvocationResult::success(std::uint32_t value) { return {value, std::nullopt}; }

  WasmInvocationResult WasmInvocationResult::failure(std::string error) { return {std::nullopt, std::move(error)}; }

  WasmInvocationResult invoke_portable_wasm(PortableWasmInstance& instance, WasmPluginExport function, std::span<const std::uint32_t> arguments) {
    try {
      return instance.invoke(function, arguments);
    } catch (const std::exception& exception) {
      return WasmInvocationResult::failure(std::string{"WASM backend invocation threw: "} + exception.what());
    } catch (...) {
      return WasmInvocationResult::failure("WASM backend invocation threw");
    }
  }

  WasmPluginQueryResult query_portable_wasm_plugin(PortableWasmInstance& instance) {
    WasmPluginQueryResult result;
    constexpr std::uint32_t descriptor_size = MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE;
    const std::array allocate_arguments{descriptor_size, MOBAGEN_WASM_EXCHANGE_ALIGNMENT};
    auto allocated = invoke_portable_wasm(instance, WasmPluginExport::Allocate, allocate_arguments);
    if (!allocated.ok()) {
      add_issue(result, WasmPluginQueryIssueCode::BackendFailure, WasmPluginExport::Allocate,
                allocated.error.has_value() ? std::move(*allocated.error) : "WASM guest allocation failed", MOBAGEN_WASM_STATUS_FAILED);
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
    auto queried = invoke_portable_wasm(instance, WasmPluginExport::Query, query_arguments);
    if (!queried.ok()) {
      add_issue(result, WasmPluginQueryIssueCode::BackendFailure, WasmPluginExport::Query,
                queried.error.has_value() ? std::move(*queried.error) : "WASM plugin query failed", MOBAGEN_WASM_STATUS_FAILED);
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

  PortableWasmPluginActivation::PortableWasmPluginActivation(std::unique_ptr<PortableWasmInstance> instance, modules::ProviderDescriptor provider)
      : instance_(std::move(instance)), provider_(std::move(provider)), owner_thread_(std::this_thread::get_id()) {}

  PortableWasmPluginActivation::~PortableWasmPluginActivation() { shutdown_noexcept(); }

  PortableWasmPluginActionResult PortableWasmPluginActivation::start() {
    PortableWasmPluginActionResult result;
    if (std::this_thread::get_id() != owner_thread_) {
      add_activation_issue(result, PortableWasmPluginIssueCode::WrongThread, WasmPluginExport::Start,
                           "WASM plugin lifecycle must run on the activation owner thread");
      return result;
    }
    if (state_ != PortableWasmPluginState::Configured) {
      add_activation_issue(result, PortableWasmPluginIssueCode::InvalidTransition, WasmPluginExport::Start,
                           "only a configured WASM plugin can be started");
      return result;
    }

    invoke_lifecycle(*instance_, WasmPluginExport::Start, result);
    if (result.ok()) {
      state_ = PortableWasmPluginState::Active;
      return result;
    }
    invoke_lifecycle(*instance_, WasmPluginExport::Quiesce, result);
    invoke_lifecycle(*instance_, WasmPluginExport::Stop, result);
    state_ = PortableWasmPluginState::Stopped;
    return result;
  }

  PortableWasmPluginActionResult PortableWasmPluginActivation::quiesce() {
    PortableWasmPluginActionResult result;
    if (std::this_thread::get_id() != owner_thread_) {
      add_activation_issue(result, PortableWasmPluginIssueCode::WrongThread, WasmPluginExport::Quiesce,
                           "WASM plugin lifecycle must run on the activation owner thread");
      return result;
    }
    if (state_ != PortableWasmPluginState::Active) {
      add_activation_issue(result, PortableWasmPluginIssueCode::InvalidTransition, WasmPluginExport::Quiesce,
                           "only an active WASM plugin can be quiesced");
      return result;
    }
    close_command_channels(result);
    invoke_lifecycle(*instance_, WasmPluginExport::Quiesce, result);
    state_ = PortableWasmPluginState::Quiesced;
    return result;
  }

  void PortableWasmPluginActivation::close_command_channels(PortableWasmPluginActionResult& result) {
    for (auto& channel : command_channels_) {
      auto closed = channel->close();
      if (!closed.ok()) {
        const auto& issue = closed.issues[0];
        add_activation_issue(result, PortableWasmPluginIssueCode::CommandChannelCloseFailed, WasmPluginExport::Deallocate, issue.message,
                             issue.status);
      }
    }
    command_channels_.clear();
  }

  PortableWasmPluginActionResult PortableWasmPluginActivation::stop() {
    PortableWasmPluginActionResult result;
    if (std::this_thread::get_id() != owner_thread_) {
      add_activation_issue(result, PortableWasmPluginIssueCode::WrongThread, WasmPluginExport::Stop,
                           "WASM plugin lifecycle must run on the activation owner thread");
      return result;
    }
    if (state_ != PortableWasmPluginState::Quiesced) {
      add_activation_issue(result, PortableWasmPluginIssueCode::InvalidTransition, WasmPluginExport::Stop,
                           "only a quiesced WASM plugin can be stopped");
      return result;
    }
    invoke_lifecycle(*instance_, WasmPluginExport::Stop, result);
    state_ = PortableWasmPluginState::Stopped;
    return result;
  }

  void PortableWasmPluginActivation::shutdown_noexcept() noexcept {
    if (state_ == PortableWasmPluginState::Stopped || std::this_thread::get_id() != owner_thread_) return;
    try {
      if (state_ == PortableWasmPluginState::Active) (void)quiesce();
      if (state_ == PortableWasmPluginState::Quiesced) (void)stop();
    } catch (...) {
    }
    state_ = PortableWasmPluginState::Stopped;
  }

  PortableWasmPluginActivationResult PortableWasmPluginActivation::activate_queried(std::unique_ptr<PortableWasmInstance> instance,
                                                                                    modules::ProviderDescriptor provider,
                                                                                    std::span<const std::byte> configuration) {
    PortableWasmPluginActivationResult result;
    if (instance == nullptr) {
      result.issues.push_back({PortableWasmPluginIssueCode::InvalidInstance,
                               WasmPluginExport::Query,
                               MOBAGEN_WASM_STATUS_INVALID_ARGUMENT,
                               "portable WASM plugin instance is null",
                               {}});
      return result;
    }

    auto configured = configure_instance(*instance, configuration);
    if (!configured.ok()) {
      result.issues = std::move(configured.issues);
      return result;
    }

    try {
      result.activation = std::unique_ptr<PortableWasmPluginActivation>(new PortableWasmPluginActivation(std::move(instance), std::move(provider)));
    } catch (const std::bad_alloc&) {
      result.issues.push_back({PortableWasmPluginIssueCode::OutOfMemory,
                               WasmPluginExport::Start,
                               MOBAGEN_WASM_STATUS_OUT_OF_MEMORY,
                               "portable WASM plugin activation ran out of memory",
                               {}});
      return result;
    }

    auto started = result.activation->start();
    if (!started.ok()) {
      result.issues = std::move(started.issues);
      result.activation.reset();
    }
    return result;
  }

  PortableWasmPluginActivationResult activate_portable_wasm_plugin(std::unique_ptr<PortableWasmInstance> instance,
                                                                   std::span<const std::byte> configuration) {
    PortableWasmPluginActivationResult result;
    if (instance == nullptr) {
      result.issues.push_back({PortableWasmPluginIssueCode::InvalidInstance,
                               WasmPluginExport::Query,
                               MOBAGEN_WASM_STATUS_INVALID_ARGUMENT,
                               "portable WASM plugin instance is null",
                               {}});
      return result;
    }

    auto queried = query_portable_wasm_plugin(*instance);
    if (!queried.ok()) {
      result.issues.push_back({PortableWasmPluginIssueCode::QueryFailed, WasmPluginExport::Query, MOBAGEN_WASM_STATUS_FAILED,
                               "portable WASM plugin query failed", std::move(queried.issues)});
      return result;
    }
    return PortableWasmPluginActivation::activate_queried(std::move(instance), std::move(*queried.provider), configuration);
  }

  PortableWasmPluginActivationResult activate_loaded_portable_wasm_plugin(LoadedPortableWasmPlugin plugin, std::span<const std::byte> configuration) {
    return PortableWasmPluginActivation::activate_queried(std::move(plugin.instance_), std::move(plugin.provider_), configuration);
  }

}  // namespace mobagen::plugins
