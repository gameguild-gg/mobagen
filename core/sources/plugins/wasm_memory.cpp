#include "wasm_memory.hpp"

#include <mobagen/plugin/wasm_abi.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace mobagen::plugins {
  namespace {

    std::optional<std::uint32_t> read_u32(std::span<const std::byte> memory, std::size_t offset) noexcept {
      if (offset > memory.size() || sizeof(std::uint32_t) > memory.size() - offset) return std::nullopt;
      std::uint32_t value = 0;
      for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        value |= std::to_integer<std::uint32_t>(memory[offset + byte]) << (byte * 8U);
      }
      return value;
    }

    WasmCommandBatchValidationResult failure(WasmMemoryIssueCode code, std::uint32_t offset, std::string message) {
      WasmCommandBatchValidationResult result;
      result.issues.push_back({code, offset, std::move(message)});
      return result;
    }

  }  // namespace

  WasmCommandBatchValidationResult validate_wasm_command_batch(std::span<const std::byte> linear_memory, std::uint32_t batch_offset) {
    if (batch_offset % alignof(std::uint32_t) != 0) {
      return failure(WasmMemoryIssueCode::Misaligned, batch_offset, "command batch header must be 4-byte aligned");
    }
    if (static_cast<std::size_t>(batch_offset) > linear_memory.size()
        || MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE > linear_memory.size() - static_cast<std::size_t>(batch_offset)) {
      return failure(WasmMemoryIssueCode::OutOfBounds, batch_offset, "command batch header is outside linear memory");
    }

    const auto struct_size = read_u32(linear_memory, batch_offset);
    const auto abi_version = read_u32(linear_memory, static_cast<std::size_t>(batch_offset) + 4);
    const auto bytes_offset = read_u32(linear_memory, static_cast<std::size_t>(batch_offset) + 8);
    const auto bytes_size = read_u32(linear_memory, static_cast<std::size_t>(batch_offset) + 12);
    const auto command_count = read_u32(linear_memory, static_cast<std::size_t>(batch_offset) + 16);
    const auto reserved = read_u32(linear_memory, static_cast<std::size_t>(batch_offset) + 20);
    if (!struct_size.has_value() || !abi_version.has_value() || !bytes_offset.has_value() || !bytes_size.has_value() || !command_count.has_value()
        || !reserved.has_value()) {
      return failure(WasmMemoryIssueCode::OutOfBounds, batch_offset, "command batch header is truncated");
    }
    if (*struct_size != MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE || *abi_version != MOBAGEN_WASM_PLUGIN_ABI_VERSION || *reserved != 0) {
      return failure(WasmMemoryIssueCode::InvalidStruct, batch_offset, "command batch size, ABI version, or reserved field is invalid");
    }
    if (*bytes_size > MOBAGEN_WASM_MAX_COMMAND_BATCH_BYTES) {
      return failure(WasmMemoryIssueCode::SizeLimit, *bytes_offset, "command batch exceeds the 16 MiB limit");
    }
    if (*bytes_offset % MOBAGEN_WASM_COMMAND_ALIGNMENT != 0) {
      return failure(WasmMemoryIssueCode::Misaligned, *bytes_offset, "command bytes must be 8-byte aligned");
    }
    if (static_cast<std::size_t>(*bytes_offset) > linear_memory.size()
        || static_cast<std::size_t>(*bytes_size) > linear_memory.size() - static_cast<std::size_t>(*bytes_offset)) {
      return failure(WasmMemoryIssueCode::OutOfBounds, *bytes_offset, "command bytes are outside linear memory");
    }
    if (*command_count > MOBAGEN_WASM_MAX_COMMANDS_PER_BATCH || *command_count > *bytes_size / MOBAGEN_WASM_COMMAND_HEADER_V1_SIZE) {
      return failure(WasmMemoryIssueCode::InvalidCommandCount, *bytes_offset, "command count cannot fit in the bounded batch");
    }

    const auto bytes = linear_memory.subspan(*bytes_offset, *bytes_size);
    std::size_t cursor = 0;
    for (std::uint32_t index = 0; index < *command_count; ++index) {
      const auto command_offset = static_cast<std::size_t>(*bytes_offset) + cursor;
      const auto command_size = read_u32(bytes, cursor);
      if (!command_size.has_value()) {
        return failure(WasmMemoryIssueCode::InvalidCommandCount, static_cast<std::uint32_t>(command_offset),
                       "command count exceeds the encoded batch");
      }
      if (*command_size < MOBAGEN_WASM_COMMAND_HEADER_V1_SIZE || *command_size > bytes.size() - cursor) {
        return failure(WasmMemoryIssueCode::InvalidCommandSize, static_cast<std::uint32_t>(command_offset),
                       "encoded command size is truncated or smaller than its header");
      }
      if (*command_size % MOBAGEN_WASM_COMMAND_ALIGNMENT != 0) {
        return failure(WasmMemoryIssueCode::Misaligned, static_cast<std::uint32_t>(command_offset),
                       "encoded command size must preserve 8-byte alignment");
      }
      cursor += *command_size;
    }
    if (cursor != bytes.size()) {
      return failure(WasmMemoryIssueCode::TrailingBytes, static_cast<std::uint32_t>(static_cast<std::size_t>(*bytes_offset) + cursor),
                     "command count does not consume the complete batch");
    }

    WasmCommandBatchValidationResult result;
    result.batch = WasmCommandBatchView{bytes, *command_count};
    return result;
  }

}  // namespace mobagen::plugins
