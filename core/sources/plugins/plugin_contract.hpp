#pragma once

#include "modules/descriptor.hpp"
#include "plugin_abi.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mobagen::plugins {

  inline constexpr std::uint32_t max_plugin_capabilities = 1024;
  inline constexpr std::size_t max_plugin_string_bytes = 512;

  enum class PluginContractIssueCode : std::uint8_t {
    unsupported_abi,
    truncated_descriptor,
    truncated_lifecycle,
    invalid_string,
    limit_exceeded,
    missing_array,
    invalid_lifecycle,
    invalid_reload_policy,
    invalid_provider_descriptor,
  };

  struct PluginContractIssue {
    PluginContractIssueCode code{};
    std::string field;
    std::string message;
  };

  struct NativePluginContract {
    modules::ProviderDescriptor provider;
    void* plugin_state{nullptr};
    MobagenPluginLifecycleV1 lifecycle{};
  };

  struct PluginContractResult {
    std::optional<NativePluginContract> contract;
    std::vector<PluginContractIssue> issues;
  };

  [[nodiscard]] PluginContractResult validate_native_plugin(const MobagenPluginDescriptorV1& descriptor);

}  // namespace mobagen::plugins
