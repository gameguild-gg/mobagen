#pragma once

#include "fixed_issue_list.hpp"
#include "wasm_memory.hpp"
#include "wasm_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>

namespace mobagen::plugins {

  enum class WasmCommandChannelIssueCode : std::uint8_t {
    InvalidCapacity,
    BackendFailure,
    AllocationFailed,
    InvalidExchangeBuffer,
    InvalidInputBatch,
    CallbackFailed,
    InvalidResult,
    InvalidOutputBatch,
    DeallocationFailed,
    WrongThread,
    Closed,
    OutOfMemory,
  };

  struct WasmCommandChannelIssue {
    WasmCommandChannelIssueCode code{};
    WasmPluginExport phase{};
    std::uint32_t status{MOBAGEN_WASM_STATUS_OK};
    std::string message;
    FixedIssueList<WasmMemoryIssue, 1> memory_issues;
  };

  struct WasmCommandChannelActionResult {
    FixedIssueList<WasmCommandChannelIssue, 1> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  struct WasmCommandProcessResult {
    /* The output view remains valid only until the next instance invocation. */
    std::optional<WasmCommandBatchView> output;
    FixedIssueList<WasmCommandChannelIssue, 1> issues;

    [[nodiscard]] bool ok() const noexcept { return output.has_value() && issues.empty(); }
  };

  struct WasmCommandChannelCreateResult;

  class WasmCommandChannel {
  public:
    WasmCommandChannel(const WasmCommandChannel&) = delete;
    WasmCommandChannel& operator=(const WasmCommandChannel&) = delete;
    WasmCommandChannel(WasmCommandChannel&&) = delete;
    WasmCommandChannel& operator=(WasmCommandChannel&&) = delete;
    ~WasmCommandChannel();

    [[nodiscard]] std::uint32_t input_capacity() const noexcept { return input_capacity_; }
    [[nodiscard]] std::uint32_t output_capacity() const noexcept { return output_capacity_; }
    [[nodiscard]] bool closed() const noexcept { return closed_; }
    [[nodiscard]] WasmCommandProcessResult process(std::span<const std::byte> input, std::uint32_t command_count);
    [[nodiscard]] WasmCommandChannelActionResult close();

  private:
    friend WasmCommandChannelCreateResult create_wasm_command_channel(PortableWasmInstance&, std::uint32_t, std::uint32_t);

    WasmCommandChannel(PortableWasmInstance& instance, std::uint32_t base_offset, std::uint32_t total_size, std::uint32_t input_batch_offset,
                       std::uint32_t input_bytes_offset, std::uint32_t input_capacity, std::uint32_t output_batch_offset,
                       std::uint32_t output_bytes_offset, std::uint32_t output_capacity, std::uint32_t result_offset) noexcept;

    PortableWasmInstance& instance_;
    std::thread::id owner_thread_;
    std::uint32_t base_offset_{};
    std::uint32_t total_size_{};
    std::uint32_t input_batch_offset_{};
    std::uint32_t input_bytes_offset_{};
    std::uint32_t input_capacity_{};
    std::uint32_t output_batch_offset_{};
    std::uint32_t output_bytes_offset_{};
    std::uint32_t output_capacity_{};
    std::uint32_t result_offset_{};
    bool closed_{false};
  };

  struct WasmCommandChannelCreateResult {
    std::unique_ptr<WasmCommandChannel> channel;
    FixedIssueList<WasmCommandChannelIssue, 2> issues;

    [[nodiscard]] bool ok() const noexcept { return channel != nullptr && issues.empty(); }
  };

  [[nodiscard]] WasmCommandChannelCreateResult create_wasm_command_channel(PortableWasmInstance& instance, std::uint32_t input_capacity,
                                                                           std::uint32_t output_capacity);

}  // namespace mobagen::plugins
