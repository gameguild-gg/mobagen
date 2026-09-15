#pragma once

#include "fixed_issue_list.hpp"
#include "wasm_command_channel.hpp"

#include <mobagen/plugin/wasm_asset_store_v1.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mobagen::plugins {

  enum class WasmAssetStoreIssueCode : std::uint8_t {
    InvalidCapacity,
    BatchFull,
    InvalidConfiguration,
    ChannelFailure,
    InvalidResponse,
    MismatchedResponse,
    OutOfMemory,
  };

  struct WasmAssetStoreIssue {
    WasmAssetStoreIssueCode code{};
    std::string message;
    FixedIssueList<WasmCommandChannelIssue, 1> channel_issues;
  };

  struct WasmAssetSeed {
    MobagenWasmAssetIdV1 id{};
    std::span<const std::byte> payload;
  };

  struct WasmAssetConfigurationResult {
    std::vector<std::byte> bytes;
    FixedIssueList<WasmAssetStoreIssue, 1> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  [[nodiscard]] WasmAssetConfigurationResult build_wasm_asset_configuration(std::span<const WasmAssetSeed> assets);

  struct WasmAssetResponseView {
    std::uint32_t opcode{};
    std::uint32_t request_id{};
    std::uint32_t status{MOBAGEN_WASM_STATUS_FAILED};
    MobagenWasmHandle32 handle{};
    std::span<const std::byte> payload;
  };

  class WasmAssetResponseBatchView {
  public:
    [[nodiscard]] std::uint32_t size() const noexcept { return command_count_; }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }
    [[nodiscard]] bool next(std::size_t& cursor, WasmAssetResponseView& response) const noexcept;

  private:
    friend struct WasmAssetStoreProcessResult;
    friend class WasmAssetCommandBuffer;

    WasmAssetResponseBatchView(std::span<const std::byte> bytes, std::uint32_t command_count) noexcept
        : bytes_(bytes), command_count_(command_count) {}

    std::span<const std::byte> bytes_;
    std::uint32_t command_count_{};
  };

  struct WasmAssetStoreProcessResult {
    std::optional<WasmAssetResponseBatchView> responses;
    FixedIssueList<WasmAssetStoreIssue, 1> issues;

    [[nodiscard]] bool ok() const noexcept { return responses.has_value() && issues.empty(); }
  };

  /*
   * Reusable, preallocated host-side command buffer. queue_* and process perform
   * no host allocation after construction as long as the configured limits are
   * respected. One process call crosses the WASM boundary for the whole batch.
   */
  class WasmAssetCommandBuffer {
  public:
    WasmAssetCommandBuffer(std::uint32_t max_commands, std::uint32_t byte_capacity);

    [[nodiscard]] bool queue_acquire(std::uint32_t request_id, const MobagenWasmAssetIdV1& id) noexcept;
    [[nodiscard]] bool queue_view(std::uint32_t request_id, MobagenWasmHandle32 handle) noexcept;
    [[nodiscard]] bool queue_release(std::uint32_t request_id, MobagenWasmHandle32 handle) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::uint32_t size() const noexcept { return command_count_; }
    [[nodiscard]] std::uint32_t max_commands() const noexcept { return max_commands_; }
    [[nodiscard]] std::uint32_t byte_capacity() const noexcept { return byte_capacity_; }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }
    [[nodiscard]] WasmAssetStoreProcessResult process(WasmCommandChannel& channel) const;

  private:
    [[nodiscard]] bool queue_handle(std::uint32_t opcode, std::uint32_t request_id, MobagenWasmHandle32 handle) noexcept;

    std::vector<std::byte> bytes_;
    std::uint32_t max_commands_{};
    std::uint32_t byte_capacity_{};
    std::uint32_t command_count_{};
  };

}  // namespace mobagen::plugins
