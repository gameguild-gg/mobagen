#include "wasm_host_imports.hpp"

#include <mobagen/plugin/wasm_abi.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace mobagen::plugins {
  namespace {

    [[nodiscard]] bool valid_status(std::uint32_t status) noexcept { return status <= MOBAGEN_WASM_STATUS_FAILED; }

    [[nodiscard]] std::optional<std::string_view> guest_text(std::span<const std::byte> memory, std::uint32_t offset,
                                                             std::uint32_t size) noexcept {
      if (size == 0) return std::string_view{};
      if (offset == MOBAGEN_WASM_NULL_OFFSET || size > MOBAGEN_WASM_MAX_STRING_BYTES || static_cast<std::size_t>(offset) > memory.size()
          || static_cast<std::size_t>(size) > memory.size() - static_cast<std::size_t>(offset)) {
        return std::nullopt;
      }
      return std::string_view{reinterpret_cast<const char*>(memory.data() + offset), size};
    }

    [[nodiscard]] bool capability_version_matches(std::string_view capability, std::uint32_t expected) noexcept {
      if (expected == 0) return false;
      const auto marker = capability.rfind(".v");
      if (marker == std::string_view::npos || marker + 2 == capability.size()) return false;
      std::uint32_t actual = 0;
      const auto version = capability.substr(marker + 2);
      const auto parsed = std::from_chars(version.data(), version.data() + version.size(), actual);
      return parsed.ec == std::errc{} && parsed.ptr == version.data() + version.size() && actual == expected;
    }

    [[nodiscard]] bool writable_region(std::span<std::byte> memory, std::uint32_t offset, std::uint32_t size,
                                       std::uint32_t alignment) noexcept {
      return offset != MOBAGEN_WASM_NULL_OFFSET && offset % alignment == 0 && static_cast<std::size_t>(offset) <= memory.size()
             && static_cast<std::size_t>(size) <= memory.size() - static_cast<std::size_t>(offset);
    }

    [[nodiscard]] bool regions_overlap(std::size_t left_offset, std::size_t left_size, std::size_t right_offset,
                                       std::size_t right_size) noexcept {
      return left_offset < right_offset + right_size && right_offset < left_offset + left_size;
    }

    void write_u32(std::span<std::byte> memory, std::size_t offset, std::uint32_t value) noexcept {
      for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        memory[offset + byte] = std::byte{static_cast<std::uint8_t>(value >> (byte * 8U))};
      }
    }

    void clear_handle(std::span<std::byte> memory, std::uint32_t offset) noexcept {
      write_u32(memory, offset, 0);
      write_u32(memory, offset + 4, 0);
    }

    void write_command_result(std::span<std::byte> memory, std::uint32_t offset, std::uint32_t status) noexcept {
      write_u32(memory, offset, MOBAGEN_WASM_COMMAND_RESULT_V1_SIZE);
      write_u32(memory, offset + 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
      write_u32(memory, offset + 8, status);
      write_u32(memory, offset + 12, 0);
      write_u32(memory, offset + 16, 0);
      write_u32(memory, offset + 20, 0);
    }

  }  // namespace

  WasmHostImports::WasmHostImports(WasmHostServices services) noexcept : services_(services), owner_thread_(std::this_thread::get_id()) {}

  bool WasmHostImports::bind(std::shared_ptr<const modules::CapabilityRegistry> registry, std::span<const std::string> permissions) {
    if (!owner_thread() || registry == nullptr) return false;
    std::vector<std::string> validated{permissions.begin(), permissions.end()};
    std::ranges::sort(validated);
    if (std::ranges::any_of(validated, [](const auto& permission) { return !modules::is_slug(permission); })
        || std::ranges::adjacent_find(validated) != validated.end()) {
      return false;
    }
    permissions_ = std::move(validated);
    registry_ = std::move(registry);
    return true;
  }

  void WasmHostImports::unbind() noexcept {
    if (!owner_thread()) return;
    registry_.reset();
    permissions_.clear();
  }

  std::uint32_t WasmHostImports::log(std::span<const std::byte> memory, std::uint32_t level, std::uint32_t message_offset,
                                     std::uint32_t message_size) const noexcept {
    if (!owner_thread()) return MOBAGEN_WASM_STATUS_FAILED;
    const auto message = guest_text(memory, message_offset, message_size);
    if (!message.has_value()) return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
    if (services_.log == nullptr) return MOBAGEN_WASM_STATUS_UNSUPPORTED;
    try {
      const auto status = services_.log(services_.state, level, *message);
      return valid_status(status) ? status : MOBAGEN_WASM_STATUS_FAILED;
    } catch (...) {
      return MOBAGEN_WASM_STATUS_FAILED;
    }
  }

  std::uint32_t WasmHostImports::find_capability(std::span<std::byte> memory, std::uint32_t capability_offset,
                                                 std::uint32_t capability_size, std::uint32_t capability_version,
                                                 std::uint32_t output_handle_offset) const noexcept {
    if (!writable_region(memory, output_handle_offset, MOBAGEN_WASM_HANDLE32_SIZE, alignof(std::uint32_t))) {
      return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
    }
    if (!owner_thread()) {
      clear_handle(memory, output_handle_offset);
      return MOBAGEN_WASM_STATUS_FAILED;
    }
    if (registry_ == nullptr) {
      clear_handle(memory, output_handle_offset);
      return MOBAGEN_WASM_STATUS_UNSUPPORTED;
    }
    const auto capability = guest_text(memory, capability_offset, capability_size);
    if (!capability.has_value() || capability->empty() || !modules::is_capability_id(*capability)) {
      return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
    }
    if (!capability_version_matches(*capability, capability_version)) {
      clear_handle(memory, output_handle_offset);
      return MOBAGEN_WASM_STATUS_NOT_FOUND;
    }
    const auto index = registry_->find_capability(*capability);
    if (!index.has_value()) {
      clear_handle(memory, output_handle_offset);
      return MOBAGEN_WASM_STATUS_NOT_FOUND;
    }
    const auto generation = registry_->generation().value;
    if (generation == 0 || generation > std::numeric_limits<std::uint32_t>::max()) {
      clear_handle(memory, output_handle_offset);
      return MOBAGEN_WASM_STATUS_FAILED;
    }
    write_u32(memory, output_handle_offset, index->value);
    write_u32(memory, output_handle_offset + 4, static_cast<std::uint32_t>(generation));
    return MOBAGEN_WASM_STATUS_OK;
  }

  std::uint32_t WasmHostImports::submit_commands(std::span<std::byte> memory, std::uint32_t input_batch_offset,
                                                 std::uint32_t result_offset) const noexcept {
    if (!writable_region(memory, result_offset, MOBAGEN_WASM_COMMAND_RESULT_V1_SIZE, MOBAGEN_WASM_EXCHANGE_ALIGNMENT)) {
      return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
    }
    if (!owner_thread()) {
      write_command_result(memory, result_offset, MOBAGEN_WASM_STATUS_FAILED);
      return MOBAGEN_WASM_STATUS_FAILED;
    }
    if (registry_ == nullptr || services_.submit_commands == nullptr) {
      write_command_result(memory, result_offset, MOBAGEN_WASM_STATUS_UNSUPPORTED);
      return MOBAGEN_WASM_STATUS_UNSUPPORTED;
    }
    const auto validated = validate_wasm_command_batch(memory, input_batch_offset);
    if (!validated.ok()) {
      write_command_result(memory, result_offset, MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
      return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
    }
    const auto command_bytes_offset = static_cast<std::size_t>(validated.batch->bytes.data() - memory.data());
    if (regions_overlap(result_offset, MOBAGEN_WASM_COMMAND_RESULT_V1_SIZE, input_batch_offset, MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE)
        || regions_overlap(result_offset, MOBAGEN_WASM_COMMAND_RESULT_V1_SIZE, command_bytes_offset, validated.batch->bytes.size())) {
      write_command_result(memory, result_offset, MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
      return MOBAGEN_WASM_STATUS_INVALID_ARGUMENT;
    }

    std::uint32_t status = MOBAGEN_WASM_STATUS_FAILED;
    try {
      const auto reported = services_.submit_commands(services_.state, *validated.batch, permissions_);
      status = valid_status(reported) ? reported : MOBAGEN_WASM_STATUS_FAILED;
    } catch (...) {
      status = MOBAGEN_WASM_STATUS_FAILED;
    }
    write_command_result(memory, result_offset, status);
    return status;
  }

}  // namespace mobagen::plugins
