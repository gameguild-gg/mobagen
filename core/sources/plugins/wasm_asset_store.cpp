#include "wasm_asset_store.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace mobagen::plugins {
  namespace {

    constexpr std::uint32_t align_command(std::uint32_t size) noexcept {
      return (size + MOBAGEN_WASM_COMMAND_ALIGNMENT - 1U) & ~(MOBAGEN_WASM_COMMAND_ALIGNMENT - 1U);
    }

    std::optional<std::uint32_t> read_u32(std::span<const std::byte> bytes, std::size_t offset) noexcept {
      if (offset > bytes.size() || sizeof(std::uint32_t) > bytes.size() - offset) {
        return std::nullopt;
      }
      std::uint32_t value = 0;
      for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + byte]) << (byte * 8U);
      }
      return value;
    }

    void write_u32(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) noexcept {
      for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        bytes[offset + byte] = std::byte{static_cast<std::uint8_t>(value >> (byte * 8U))};
      }
    }

    template <typename Result> void add_issue(Result& result, WasmAssetStoreIssueCode code, std::string message) {
      result.issues.push_back({code, std::move(message), {}});
    }

    bool valid_status(std::uint32_t status) noexcept { return status <= MOBAGEN_WASM_STATUS_FAILED; }

    std::optional<std::uint32_t> expected_result_opcode(std::uint32_t command) noexcept {
      switch (command) {
        case MOBAGEN_WASM_ASSET_COMMAND_ACQUIRE:
          return MOBAGEN_WASM_ASSET_RESULT_ACQUIRE;
        case MOBAGEN_WASM_ASSET_COMMAND_VIEW:
          return MOBAGEN_WASM_ASSET_RESULT_VIEW;
        case MOBAGEN_WASM_ASSET_COMMAND_RELEASE:
          return MOBAGEN_WASM_ASSET_RESULT_RELEASE;
        default:
          return std::nullopt;
      }
    }

    bool decode_response(std::span<const std::byte> bytes, std::size_t offset, WasmAssetResponseView& response) noexcept {
      const auto byte_size = read_u32(bytes, offset);
      const auto opcode = read_u32(bytes, offset + 4);
      const auto request_id = read_u32(bytes, offset + 8);
      const auto status = read_u32(bytes, offset + 12);
      if (!byte_size.has_value() || !opcode.has_value() || !request_id.has_value() || !status.has_value() || !valid_status(*status) || *byte_size == 0
          || *byte_size % MOBAGEN_WASM_COMMAND_ALIGNMENT != 0 || offset > bytes.size() || *byte_size > bytes.size() - offset) {
        return false;
      }

      response = {
          .opcode = *opcode,
          .request_id = *request_id,
          .status = *status,
      };
      if (*opcode == MOBAGEN_WASM_ASSET_RESULT_ACQUIRE) {
        if (*byte_size != MOBAGEN_WASM_ASSET_ACQUIRE_RESULT_V1_SIZE) return false;
        response.handle = {*read_u32(bytes, offset + 16), *read_u32(bytes, offset + 20)};
        return true;
      }
      if (*opcode == MOBAGEN_WASM_ASSET_RESULT_RELEASE) {
        return *byte_size == MOBAGEN_WASM_ASSET_RELEASE_RESULT_V1_SIZE;
      }
      if (*opcode != MOBAGEN_WASM_ASSET_RESULT_VIEW || *byte_size < MOBAGEN_WASM_ASSET_VIEW_RESULT_V1_SIZE) {
        return false;
      }
      const auto index = read_u32(bytes, offset + 16);
      const auto generation = read_u32(bytes, offset + 20);
      const auto payload_size = read_u32(bytes, offset + 24);
      const auto reserved = read_u32(bytes, offset + 28);
      if (!index.has_value() || !generation.has_value() || !payload_size.has_value() || !reserved.has_value() || *reserved != 0) {
        return false;
      }
      const auto expected_size = static_cast<std::uint64_t>(MOBAGEN_WASM_ASSET_VIEW_RESULT_V1_SIZE) + *payload_size;
      if (expected_size > std::numeric_limits<std::uint32_t>::max() || align_command(static_cast<std::uint32_t>(expected_size)) != *byte_size) {
        return false;
      }
      response.handle = {*index, *generation};
      response.payload = bytes.subspan(offset + MOBAGEN_WASM_ASSET_VIEW_RESULT_V1_SIZE, *payload_size);
      return true;
    }

  }  // namespace

  WasmAssetConfigurationResult build_wasm_asset_configuration(std::span<const WasmAssetSeed> assets) {
    WasmAssetConfigurationResult result;
    if (assets.size() > MOBAGEN_WASM_ASSET_MAX_CONFIGURED_ASSETS) {
      add_issue(result, WasmAssetStoreIssueCode::InvalidConfiguration, "portable asset configuration exceeds the asset-count limit");
      return result;
    }

    std::uint64_t total = MOBAGEN_WASM_ASSET_CONFIGURATION_V1_SIZE;
    std::uint64_t payload_total = 0;
    for (const auto& asset : assets) {
      if (asset.payload.size() > MOBAGEN_WASM_ASSET_MAX_CONFIGURED_BYTES) {
        add_issue(result, WasmAssetStoreIssueCode::InvalidConfiguration, "portable asset configuration exceeds the byte limit");
        return result;
      }
      payload_total += asset.payload.size();
      total += align_command(MOBAGEN_WASM_ASSET_CONFIGURATION_ENTRY_V1_SIZE + static_cast<std::uint32_t>(asset.payload.size()));
      if (payload_total > MOBAGEN_WASM_ASSET_MAX_CONFIGURED_BYTES || total > MOBAGEN_WASM_MAX_CONFIGURATION_BYTES) {
        add_issue(result, WasmAssetStoreIssueCode::InvalidConfiguration, "portable asset configuration exceeds the byte limit");
        return result;
      }
    }

    try {
      result.bytes.resize(static_cast<std::size_t>(total));
    } catch (const std::bad_alloc&) {
      add_issue(result, WasmAssetStoreIssueCode::OutOfMemory, "portable asset configuration allocation failed");
      return result;
    }
    write_u32(result.bytes, 0, MOBAGEN_WASM_ASSET_CONFIGURATION_V1_SIZE);
    write_u32(result.bytes, 4, MOBAGEN_WASM_ASSET_STORE_V1_PROTOCOL_VERSION);
    write_u32(result.bytes, 8, static_cast<std::uint32_t>(assets.size()));
    write_u32(result.bytes, 12, 0);
    std::size_t cursor = MOBAGEN_WASM_ASSET_CONFIGURATION_V1_SIZE;
    for (const auto& asset : assets) {
      std::memcpy(result.bytes.data() + cursor, asset.id.bytes, sizeof(asset.id.bytes));
      write_u32(result.bytes, cursor + MOBAGEN_WASM_ASSET_ID_V1_SIZE, static_cast<std::uint32_t>(asset.payload.size()));
      write_u32(result.bytes, cursor + MOBAGEN_WASM_ASSET_ID_V1_SIZE + 4, 0);
      std::ranges::copy(asset.payload, result.bytes.begin() + cursor + MOBAGEN_WASM_ASSET_CONFIGURATION_ENTRY_V1_SIZE);
      cursor += align_command(MOBAGEN_WASM_ASSET_CONFIGURATION_ENTRY_V1_SIZE + static_cast<std::uint32_t>(asset.payload.size()));
    }
    return result;
  }

  bool WasmAssetResponseBatchView::next(std::size_t& cursor, WasmAssetResponseView& response) const noexcept {
    if (cursor >= bytes_.size()) return false;
    const auto size = read_u32(bytes_, cursor);
    if (!size.has_value() || !decode_response(bytes_, cursor, response)) return false;
    cursor += *size;
    return true;
  }

  WasmAssetCommandBuffer::WasmAssetCommandBuffer(std::uint32_t max_commands, std::uint32_t byte_capacity)
      : max_commands_(max_commands), byte_capacity_(byte_capacity) {
    if (max_commands == 0 || max_commands > MOBAGEN_WASM_MAX_COMMANDS_PER_BATCH || byte_capacity == 0
        || byte_capacity > MOBAGEN_WASM_MAX_COMMAND_BATCH_BYTES) {
      throw std::invalid_argument{"invalid WASM asset command buffer capacity"};
    }
    bytes_.reserve(byte_capacity);
  }

  bool WasmAssetCommandBuffer::queue_acquire(std::uint32_t request_id, const MobagenWasmAssetIdV1& id) noexcept {
    if (command_count_ == max_commands_ || bytes_.size() + MOBAGEN_WASM_ASSET_ACQUIRE_COMMAND_V1_SIZE > byte_capacity_) {
      return false;
    }
    const auto offset = bytes_.size();
    bytes_.resize(offset + MOBAGEN_WASM_ASSET_ACQUIRE_COMMAND_V1_SIZE);
    write_u32(bytes_, offset, MOBAGEN_WASM_ASSET_ACQUIRE_COMMAND_V1_SIZE);
    write_u32(bytes_, offset + 4, MOBAGEN_WASM_ASSET_COMMAND_ACQUIRE);
    write_u32(bytes_, offset + 8, request_id);
    write_u32(bytes_, offset + 12, 0);
    std::memcpy(bytes_.data() + offset + 16, id.bytes, sizeof(id.bytes));
    ++command_count_;
    return true;
  }

  bool WasmAssetCommandBuffer::queue_handle(std::uint32_t opcode, std::uint32_t request_id, MobagenWasmHandle32 handle) noexcept {
    if (command_count_ == max_commands_ || bytes_.size() + MOBAGEN_WASM_ASSET_HANDLE_COMMAND_V1_SIZE > byte_capacity_) {
      return false;
    }
    const auto offset = bytes_.size();
    bytes_.resize(offset + MOBAGEN_WASM_ASSET_HANDLE_COMMAND_V1_SIZE);
    write_u32(bytes_, offset, MOBAGEN_WASM_ASSET_HANDLE_COMMAND_V1_SIZE);
    write_u32(bytes_, offset + 4, opcode);
    write_u32(bytes_, offset + 8, request_id);
    write_u32(bytes_, offset + 12, 0);
    write_u32(bytes_, offset + 16, handle.index);
    write_u32(bytes_, offset + 20, handle.generation);
    ++command_count_;
    return true;
  }

  bool WasmAssetCommandBuffer::queue_view(std::uint32_t request_id, MobagenWasmHandle32 handle) noexcept {
    return queue_handle(MOBAGEN_WASM_ASSET_COMMAND_VIEW, request_id, handle);
  }

  bool WasmAssetCommandBuffer::queue_release(std::uint32_t request_id, MobagenWasmHandle32 handle) noexcept {
    return queue_handle(MOBAGEN_WASM_ASSET_COMMAND_RELEASE, request_id, handle);
  }

  void WasmAssetCommandBuffer::clear() noexcept {
    bytes_.clear();
    command_count_ = 0;
  }

  WasmAssetStoreProcessResult WasmAssetCommandBuffer::process(WasmCommandChannel& channel) const {
    WasmAssetStoreProcessResult result;
    if (command_count_ == 0) {
      add_issue(result, WasmAssetStoreIssueCode::InvalidCapacity, "portable asset command batch is empty");
      return result;
    }
    auto processed = channel.process(bytes_, command_count_);
    if (!processed.ok()) {
      auto issue = WasmAssetStoreIssue{
          .code = WasmAssetStoreIssueCode::ChannelFailure,
          .message = "portable asset command channel failed",
      };
      if (!processed.issues.empty()) issue.channel_issues.push_back(processed.issues[0]);
      result.issues.push_back(std::move(issue));
      return result;
    }

    const auto output = *processed.output;
    if (output.command_count != command_count_) {
      add_issue(result, WasmAssetStoreIssueCode::InvalidResponse, "portable asset provider returned the wrong response count");
      return result;
    }
    std::size_t input_cursor = 0;
    std::size_t output_cursor = 0;
    for (std::uint32_t index = 0; index < command_count_; ++index) {
      const auto input_size = read_u32(bytes_, input_cursor);
      const auto input_opcode = read_u32(bytes_, input_cursor + 4);
      const auto input_request = read_u32(bytes_, input_cursor + 8);
      WasmAssetResponseView response;
      if (!input_size.has_value() || !input_opcode.has_value() || !input_request.has_value()
          || !decode_response(output.bytes, output_cursor, response)) {
        add_issue(result, WasmAssetStoreIssueCode::InvalidResponse, "portable asset provider returned a malformed response");
        return result;
      }
      const auto expected_opcode = expected_result_opcode(*input_opcode);
      if (!expected_opcode.has_value() || response.opcode != *expected_opcode || response.request_id != *input_request) {
        add_issue(result, WasmAssetStoreIssueCode::MismatchedResponse, "portable asset response does not match its request");
        return result;
      }
      input_cursor += *input_size;
      output_cursor += *read_u32(output.bytes, output_cursor);
    }
    if (input_cursor != bytes_.size() || output_cursor != output.bytes.size()) {
      add_issue(result, WasmAssetStoreIssueCode::InvalidResponse, "portable asset response batch has trailing bytes");
      return result;
    }
    result.responses = WasmAssetResponseBatchView{output.bytes, output.command_count};
    return result;
  }

}  // namespace mobagen::plugins
