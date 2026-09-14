#include "wasm_plugin_contract.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#  include <TargetConditionals.h>
#endif

namespace mobagen::plugins {
  namespace {

    struct EncodedSpan {
      std::uint32_t offset{};
      std::uint32_t size{};
    };

    void add_issue(WasmPluginContractResult& result, WasmPluginContractIssueCode code, std::string field, std::string message) {
      result.issues.push_back({code, std::move(field), std::move(message)});
    }

    std::optional<std::uint32_t> read_u32(std::span<const std::byte> memory, std::size_t offset) noexcept {
      if (offset > memory.size() || sizeof(std::uint32_t) > memory.size() - offset) return std::nullopt;
      std::uint32_t value = 0;
      for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        value |= std::to_integer<std::uint32_t>(memory[offset + byte]) << (byte * 8U);
      }
      return value;
    }

    EncodedSpan read_encoded_span(std::span<const std::byte> memory, std::size_t offset) {
      return {*read_u32(memory, offset), *read_u32(memory, offset + sizeof(std::uint32_t))};
    }

    std::optional<std::string> decode_string(std::span<const std::byte> memory, EncodedSpan encoded, std::string field,
                                             WasmPluginContractResult& result) {
      if (encoded.size > MOBAGEN_WASM_MAX_STRING_BYTES) {
        add_issue(result, WasmPluginContractIssueCode::LimitExceeded, std::move(field), "WASM descriptor string exceeds the 4096-byte limit");
        return std::nullopt;
      }
      if (encoded.size == 0) return std::string{};
      if (static_cast<std::size_t>(encoded.offset) > memory.size()
          || static_cast<std::size_t>(encoded.size) > memory.size() - static_cast<std::size_t>(encoded.offset)) {
        add_issue(result, WasmPluginContractIssueCode::OutOfBounds, std::move(field), "WASM descriptor string is outside linear memory");
        return std::nullopt;
      }

      const auto bytes = memory.subspan(encoded.offset, encoded.size);
      if (std::ranges::find(bytes, std::byte{}) != bytes.end()) {
        add_issue(result, WasmPluginContractIssueCode::InvalidString, std::move(field), "WASM descriptor string contains an embedded null byte");
        return std::nullopt;
      }
      return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    std::optional<std::vector<std::string>> decode_array(std::span<const std::byte> memory, EncodedSpan encoded, std::string_view field,
                                                         std::uint32_t& total_entries, WasmPluginContractResult& result) {
      if (encoded.size > MOBAGEN_WASM_MAX_DESCRIPTOR_ENTRIES || total_entries > MOBAGEN_WASM_MAX_DESCRIPTOR_ENTRIES - encoded.size) {
        add_issue(result, WasmPluginContractIssueCode::LimitExceeded, std::string{field},
                  "WASM descriptor arrays exceed the aggregate 1024-entry limit");
        return std::nullopt;
      }
      if (encoded.size == 0) return std::vector<std::string>{};
      if (encoded.offset % alignof(std::uint32_t) != 0) {
        add_issue(result, WasmPluginContractIssueCode::Misaligned, std::string{field}, "WASM descriptor array must be 4-byte aligned");
        return std::nullopt;
      }

      const auto byte_size = static_cast<std::size_t>(encoded.size) * MOBAGEN_WASM_SPAN32_SIZE;
      if (static_cast<std::size_t>(encoded.offset) > memory.size() || byte_size > memory.size() - static_cast<std::size_t>(encoded.offset)) {
        add_issue(result, WasmPluginContractIssueCode::OutOfBounds, std::string{field}, "WASM descriptor array is outside linear memory");
        return std::nullopt;
      }

      total_entries += encoded.size;
      std::vector<std::string> values;
      values.reserve(encoded.size);
      bool valid = true;
      for (std::uint32_t index = 0; index < encoded.size; ++index) {
        const auto item_offset = static_cast<std::size_t>(encoded.offset) + static_cast<std::size_t>(index) * MOBAGEN_WASM_SPAN32_SIZE;
        auto value = decode_string(memory, read_encoded_span(memory, item_offset), std::string{field} + '[' + std::to_string(index) + ']', result);
        if (!value.has_value()) {
          valid = false;
          continue;
        }
        values.push_back(std::move(*value));
      }
      if (!valid) return std::nullopt;
      return values;
    }

    std::optional<modules::ReloadPolicy> decode_reload_policy(std::uint32_t value) noexcept {
      switch (value) {
        case MOBAGEN_WASM_RELOAD_NEVER:
          return modules::ReloadPolicy::Never;
        case MOBAGEN_WASM_RELOAD_RESTART:
          return modules::ReloadPolicy::Restart;
        case MOBAGEN_WASM_RELOAD_SAFE_POINT:
          return modules::ReloadPolicy::SafePoint;
        default:
          return std::nullopt;
      }
    }

    modules::TargetPlatform current_target() noexcept {
#if defined(__EMSCRIPTEN__)
      return modules::TargetPlatform::Web;
#elif defined(_WIN32)
      return modules::TargetPlatform::Windows;
#elif defined(__ANDROID__)
      return modules::TargetPlatform::Android;
#elif defined(__APPLE__) && TARGET_OS_IPHONE
      return modules::TargetPlatform::IOS;
#elif defined(__APPLE__)
      return modules::TargetPlatform::MacOS;
#else
      return modules::TargetPlatform::Linux;
#endif
    }

  }  // namespace

  WasmPluginContractResult decode_wasm_plugin_descriptor(std::span<const std::byte> linear_memory, std::uint32_t descriptor_offset) {
    WasmPluginContractResult result;
    if (descriptor_offset % alignof(std::uint32_t) != 0) {
      add_issue(result, WasmPluginContractIssueCode::Misaligned, "descriptor", "WASM plugin descriptor must be 4-byte aligned");
      return result;
    }
    if (static_cast<std::size_t>(descriptor_offset) > linear_memory.size()
        || MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE > linear_memory.size() - static_cast<std::size_t>(descriptor_offset)) {
      add_issue(result, WasmPluginContractIssueCode::OutOfBounds, "descriptor", "WASM plugin descriptor is outside linear memory");
      return result;
    }

    const auto base = static_cast<std::size_t>(descriptor_offset);
    const auto struct_size = *read_u32(linear_memory, base);
    const auto abi_version = *read_u32(linear_memory, base + 4);
    if (abi_version != MOBAGEN_WASM_PLUGIN_ABI_VERSION) {
      add_issue(result, WasmPluginContractIssueCode::UnsupportedAbi, "abi_version", "WASM plugin ABI version is not supported");
    }
    if (struct_size != MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE) {
      add_issue(result, WasmPluginContractIssueCode::InvalidStruct, "struct_size", "WASM plugin descriptor size does not match ABI v1");
    }
    if (!result.issues.empty()) return result;

    auto id = decode_string(linear_memory, read_encoded_span(linear_memory, base + 8), "id", result);
    const auto version_major = *read_u32(linear_memory, base + 16);
    const auto version_minor = *read_u32(linear_memory, base + 20);
    const auto version_patch = *read_u32(linear_memory, base + 24);
    const auto reload = decode_reload_policy(*read_u32(linear_memory, base + 28));
    if (!reload.has_value()) {
      add_issue(result, WasmPluginContractIssueCode::InvalidReloadPolicy, "reload_policy", "WASM plugin reload policy is not supported");
    }

    std::uint32_t total_entries = 0;
    auto provides = decode_array(linear_memory, read_encoded_span(linear_memory, base + 32), "provides", total_entries, result);
    auto required = decode_array(linear_memory, read_encoded_span(linear_memory, base + 40), "required", total_entries, result);
    auto optional = decode_array(linear_memory, read_encoded_span(linear_memory, base + 48), "optional", total_entries, result);
    auto conflicts = decode_array(linear_memory, read_encoded_span(linear_memory, base + 56), "conflicts", total_entries, result);
    auto configuration_schema = decode_string(linear_memory, read_encoded_span(linear_memory, base + 64), "configuration_schema", result);
    auto permissions = decode_array(linear_memory, read_encoded_span(linear_memory, base + 72), "permissions", total_entries, result);
    if (!result.issues.empty() || !id.has_value() || !provides.has_value() || !required.has_value() || !optional.has_value() || !conflicts.has_value()
        || !configuration_schema.has_value() || !permissions.has_value() || !reload.has_value()) {
      return result;
    }

    modules::ProviderDescriptor provider{
        .id = std::move(*id),
        .version = {version_major, version_minor, version_patch},
        .provides = std::move(*provides),
        .required = std::move(*required),
        .optional = std::move(*optional),
        .conflicts = std::move(*conflicts),
        .targets = {current_target()},
        .linkages = {modules::LinkageMode::Wasm},
        .reload = *reload,
        .configuration_schema = std::move(*configuration_schema),
        .permissions = std::move(*permissions),
    };
    for (const auto& issue : modules::validate(provider)) {
      add_issue(result, WasmPluginContractIssueCode::InvalidProviderDescriptor, issue.field, issue.message);
    }
    if (result.issues.empty()) result.provider = std::move(provider);
    return result;
  }

}  // namespace mobagen::plugins
