#pragma once

#include <mobagen/plugin/wasm_abi.h>

#include "modules/descriptor.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mobagen::plugins {

  enum class WasmPluginContractIssueCode : std::uint8_t {
    OutOfBounds,
    Misaligned,
    UnsupportedAbi,
    InvalidStruct,
    LimitExceeded,
    InvalidString,
    InvalidReloadPolicy,
    InvalidProviderDescriptor,
  };

  struct WasmPluginContractIssue {
    WasmPluginContractIssueCode code{};
    std::string field;
    std::string message;
  };

  struct WasmPluginContractResult {
    std::optional<modules::ProviderDescriptor> provider;
    std::vector<WasmPluginContractIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return provider.has_value() && issues.empty(); }
  };

  [[nodiscard]] WasmPluginContractResult decode_wasm_plugin_descriptor(std::span<const std::byte> linear_memory, std::uint32_t descriptor_offset);

}  // namespace mobagen::plugins
