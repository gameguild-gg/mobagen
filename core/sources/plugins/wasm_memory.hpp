#pragma once

#include <mobagen/plugin/wasm_abi.h>

#include "fixed_issue_list.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace mobagen::plugins {

  enum class WasmMemoryIssueCode : std::uint8_t {
    OutOfBounds,
    Misaligned,
    SizeLimit,
    InvalidStruct,
    InvalidCommandCount,
    InvalidCommandSize,
    TrailingBytes,
  };

  struct WasmMemoryIssue {
    WasmMemoryIssueCode code{};
    std::uint32_t offset{};
    std::string message;
  };

  struct WasmCommandBatchView {
    std::span<const std::byte> bytes;
    std::uint32_t command_count{};
  };

  struct WasmCommandBatchValidationResult {
    std::optional<WasmCommandBatchView> batch;
    FixedIssueList<WasmMemoryIssue, 1> issues;

    [[nodiscard]] bool ok() const noexcept { return batch.has_value() && issues.empty(); }
  };

  [[nodiscard]] WasmCommandBatchValidationResult validate_wasm_command_batch(std::span<const std::byte> linear_memory, std::uint32_t batch_offset);

}  // namespace mobagen::plugins
