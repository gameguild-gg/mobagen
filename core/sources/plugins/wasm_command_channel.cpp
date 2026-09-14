#include "wasm_command_channel.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace mobagen::plugins {
  namespace {

    template <typename Result> void add_issue(Result& result, WasmCommandChannelIssueCode code, WasmPluginExport phase, std::string message,
                                              std::uint32_t status = MOBAGEN_WASM_STATUS_OK, FixedIssueList<WasmMemoryIssue, 1> memory_issues = {}) {
      result.issues.push_back({code, phase, status, std::move(message), std::move(memory_issues)});
    }

    std::optional<std::uint32_t> read_u32(std::span<const std::byte> memory, std::size_t offset) noexcept {
      if (offset > memory.size() || sizeof(std::uint32_t) > memory.size() - offset) return std::nullopt;
      std::uint32_t value = 0;
      for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        value |= std::to_integer<std::uint32_t>(memory[offset + byte]) << (byte * 8U);
      }
      return value;
    }

    void write_u32(std::span<std::byte> memory, std::size_t offset, std::uint32_t value) noexcept {
      for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        memory[offset + byte] = std::byte{static_cast<std::uint8_t>(value >> (byte * 8U))};
      }
    }

    constexpr std::uint64_t align_exchange(std::uint64_t value) noexcept {
      return (value + MOBAGEN_WASM_EXCHANGE_ALIGNMENT - 1U) & ~(static_cast<std::uint64_t>(MOBAGEN_WASM_EXCHANGE_ALIGNMENT) - 1U);
    }

    bool exchange_is_valid(std::span<const std::byte> memory, std::uint32_t offset, std::uint32_t size) noexcept {
      return offset != MOBAGEN_WASM_NULL_OFFSET && offset % MOBAGEN_WASM_EXCHANGE_ALIGNMENT == 0 && static_cast<std::size_t>(offset) <= memory.size()
             && static_cast<std::size_t>(size) <= memory.size() - static_cast<std::size_t>(offset);
    }

    template <typename Result> void release_region(PortableWasmInstance& instance, std::uint32_t offset, std::uint32_t size, Result& result) {
      const std::array arguments{offset, size, MOBAGEN_WASM_EXCHANGE_ALIGNMENT};
      auto released = invoke_portable_wasm(instance, WasmPluginExport::Deallocate, arguments);
      if (!released.ok()) {
        add_issue(result, WasmCommandChannelIssueCode::DeallocationFailed, WasmPluginExport::Deallocate,
                  released.error.has_value() ? std::move(*released.error) : "WASM command exchange deallocation failed", MOBAGEN_WASM_STATUS_FAILED);
      } else if (*released.value != MOBAGEN_WASM_STATUS_OK) {
        add_issue(result, WasmCommandChannelIssueCode::DeallocationFailed, WasmPluginExport::Deallocate,
                  "WASM command exchange deallocation callback reported failure", *released.value);
      }
    }

  }  // namespace

  WasmCommandChannel::WasmCommandChannel(PortableWasmInstance& instance, std::uint32_t base_offset, std::uint32_t total_size,
                                         std::uint32_t input_batch_offset, std::uint32_t input_bytes_offset, std::uint32_t input_capacity,
                                         std::uint32_t output_batch_offset, std::uint32_t output_bytes_offset, std::uint32_t output_capacity,
                                         std::uint32_t result_offset) noexcept
      : instance_(instance),
        owner_thread_(std::this_thread::get_id()),
        base_offset_(base_offset),
        total_size_(total_size),
        input_batch_offset_(input_batch_offset),
        input_bytes_offset_(input_bytes_offset),
        input_capacity_(input_capacity),
        output_batch_offset_(output_batch_offset),
        output_bytes_offset_(output_bytes_offset),
        output_capacity_(output_capacity),
        result_offset_(result_offset) {}

  WasmCommandChannel::~WasmCommandChannel() {
    if (!closed_ && std::this_thread::get_id() == owner_thread_) {
      try {
        (void)close();
      } catch (...) {
      }
    }
  }

  WasmCommandChannelActionResult WasmCommandChannel::close() {
    WasmCommandChannelActionResult result;
    if (closed_) return result;
    if (std::this_thread::get_id() != owner_thread_) {
      add_issue(result, WasmCommandChannelIssueCode::WrongThread, WasmPluginExport::Deallocate,
                "WASM command channel must be closed on its owner thread");
      return result;
    }
    closed_ = true;
    release_region(instance_, base_offset_, total_size_, result);
    return result;
  }

  WasmCommandChannelOpenResult PortableWasmPluginActivation::open_command_channel(std::uint32_t input_capacity, std::uint32_t output_capacity) {
    WasmCommandChannelOpenResult result;
    if (std::this_thread::get_id() != owner_thread_) {
      add_issue(result, WasmCommandChannelIssueCode::WrongThread, WasmPluginExport::Allocate,
                "WASM command channels must be opened on the activation owner thread");
      return result;
    }
    if (state_ != PortableWasmPluginState::Active) {
      add_issue(result, WasmCommandChannelIssueCode::InvalidState, WasmPluginExport::Allocate,
                "WASM command channels can only be opened for an active plugin");
      return result;
    }

    auto created = create_wasm_command_channel(*instance_, input_capacity, output_capacity);
    if (!created.ok()) {
      result.issues = std::move(created.issues);
      return result;
    }

    auto* borrowed = created.channel.get();
    try {
      command_channels_.push_back(std::move(created.channel));
      result.channel = borrowed;
    } catch (const std::bad_alloc&) {
      add_issue(result, WasmCommandChannelIssueCode::OutOfMemory, WasmPluginExport::Allocate, "WASM activation could not retain the command channel",
                MOBAGEN_WASM_STATUS_OUT_OF_MEMORY);
      const auto closed = created.channel->close();
      if (!closed.ok()) result.issues.push_back(closed.issues[0]);
    }
    return result;
  }

  WasmCommandProcessResult WasmCommandChannel::process(std::span<const std::byte> input, std::uint32_t command_count) {
    WasmCommandProcessResult result;
    if (closed_) {
      add_issue(result, WasmCommandChannelIssueCode::Closed, WasmPluginExport::Process, "WASM command channel is closed");
      return result;
    }
    if (std::this_thread::get_id() != owner_thread_) {
      add_issue(result, WasmCommandChannelIssueCode::WrongThread, WasmPluginExport::Process,
                "WASM command channel must be processed on its owner thread");
      return result;
    }
    if (input.size() > input_capacity_ || input.size() > MOBAGEN_WASM_MAX_COMMAND_BATCH_BYTES
        || command_count > MOBAGEN_WASM_MAX_COMMANDS_PER_BATCH) {
      add_issue(result, WasmCommandChannelIssueCode::InvalidInputBatch, WasmPluginExport::Process,
                "input command batch exceeds the channel capacity or ABI limits", MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
      return result;
    }

    auto memory = instance_.writable_memory();
    if (!exchange_is_valid(memory, base_offset_, total_size_)) {
      add_issue(result, WasmCommandChannelIssueCode::InvalidExchangeBuffer, WasmPluginExport::Process,
                "WASM command exchange is no longer inside linear memory", MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
      return result;
    }

    std::ranges::copy(input, memory.begin() + input_bytes_offset_);
    write_u32(memory, input_batch_offset_, MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE);
    write_u32(memory, input_batch_offset_ + 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
    write_u32(memory, input_batch_offset_ + 8, input_bytes_offset_);
    write_u32(memory, input_batch_offset_ + 12, static_cast<std::uint32_t>(input.size()));
    write_u32(memory, input_batch_offset_ + 16, command_count);
    write_u32(memory, input_batch_offset_ + 20, 0);
    auto validated_input = validate_wasm_command_batch(memory, input_batch_offset_);
    if (!validated_input.ok()) {
      add_issue(result, WasmCommandChannelIssueCode::InvalidInputBatch, WasmPluginExport::Process, "input command batch framing is invalid",
                MOBAGEN_WASM_STATUS_INVALID_ARGUMENT, std::move(validated_input.issues));
      return result;
    }

    write_u32(memory, output_batch_offset_, MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE);
    write_u32(memory, output_batch_offset_ + 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
    write_u32(memory, output_batch_offset_ + 8, output_bytes_offset_);
    write_u32(memory, output_batch_offset_ + 12, output_capacity_);
    write_u32(memory, output_batch_offset_ + 16, 0);
    write_u32(memory, output_batch_offset_ + 20, 0);
    write_u32(memory, result_offset_, MOBAGEN_WASM_COMMAND_RESULT_V1_SIZE);
    write_u32(memory, result_offset_ + 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
    write_u32(memory, result_offset_ + 8, MOBAGEN_WASM_STATUS_FAILED);
    write_u32(memory, result_offset_ + 12, 0);
    write_u32(memory, result_offset_ + 16, 0);
    write_u32(memory, result_offset_ + 20, 0);

    const std::array arguments{input_batch_offset_, output_batch_offset_, result_offset_};
    auto invoked = invoke_portable_wasm(instance_, WasmPluginExport::Process, arguments);
    if (!invoked.ok()) {
      add_issue(result, WasmCommandChannelIssueCode::BackendFailure, WasmPluginExport::Process,
                invoked.error.has_value() ? std::move(*invoked.error) : "WASM command processing failed", MOBAGEN_WASM_STATUS_FAILED);
      return result;
    }
    if (*invoked.value != MOBAGEN_WASM_STATUS_OK) {
      add_issue(result, WasmCommandChannelIssueCode::CallbackFailed, WasmPluginExport::Process, "WASM command processing callback reported failure",
                *invoked.value);
      return result;
    }

    const auto output_memory = instance_.memory();
    if (!exchange_is_valid(output_memory, base_offset_, total_size_)) {
      add_issue(result, WasmCommandChannelIssueCode::InvalidExchangeBuffer, WasmPluginExport::Process,
                "WASM command exchange moved outside linear memory during processing", MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
      return result;
    }
    const auto result_size = read_u32(output_memory, result_offset_);
    const auto result_abi = read_u32(output_memory, result_offset_ + 4);
    const auto status = read_u32(output_memory, result_offset_ + 8);
    const auto bytes_written = read_u32(output_memory, result_offset_ + 12);
    const auto commands_written = read_u32(output_memory, result_offset_ + 16);
    const auto reserved = read_u32(output_memory, result_offset_ + 20);
    if (!result_size.has_value() || !result_abi.has_value() || !status.has_value() || !bytes_written.has_value() || !commands_written.has_value()
        || !reserved.has_value() || *result_size != MOBAGEN_WASM_COMMAND_RESULT_V1_SIZE || *result_abi != MOBAGEN_WASM_PLUGIN_ABI_VERSION
        || *reserved != 0) {
      add_issue(result, WasmCommandChannelIssueCode::InvalidResult, WasmPluginExport::Process, "WASM command result header is invalid",
                MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
      return result;
    }
    if (*status != MOBAGEN_WASM_STATUS_OK) {
      add_issue(result, WasmCommandChannelIssueCode::CallbackFailed, WasmPluginExport::Process, "WASM command result reported failure", *status);
      return result;
    }
    if (*bytes_written > output_capacity_ || *commands_written > MOBAGEN_WASM_MAX_COMMANDS_PER_BATCH) {
      add_issue(result, WasmCommandChannelIssueCode::InvalidResult, WasmPluginExport::Process,
                "WASM command result exceeds the reserved output capacity", MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
      return result;
    }

    const auto output_bytes_offset = read_u32(output_memory, output_batch_offset_ + 8);
    const auto output_bytes_size = read_u32(output_memory, output_batch_offset_ + 12);
    const auto output_count = read_u32(output_memory, output_batch_offset_ + 16);
    if (!output_bytes_offset.has_value() || !output_bytes_size.has_value() || !output_count.has_value()
        || *output_bytes_offset != output_bytes_offset_ || *output_bytes_size != *bytes_written || *output_count != *commands_written) {
      add_issue(result, WasmCommandChannelIssueCode::InvalidOutputBatch, WasmPluginExport::Process,
                "WASM output batch does not match its reserved span and result", MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
      return result;
    }

    auto validated_output = validate_wasm_command_batch(output_memory, output_batch_offset_);
    if (!validated_output.ok()) {
      add_issue(result, WasmCommandChannelIssueCode::InvalidOutputBatch, WasmPluginExport::Process, "WASM output command framing is invalid",
                MOBAGEN_WASM_STATUS_INVALID_ARGUMENT, std::move(validated_output.issues));
      return result;
    }
    result.output = *validated_output.batch;
    return result;
  }

  WasmCommandChannelCreateResult create_wasm_command_channel(PortableWasmInstance& instance, std::uint32_t input_capacity,
                                                             std::uint32_t output_capacity) {
    WasmCommandChannelCreateResult result;
    if (input_capacity > MOBAGEN_WASM_MAX_COMMAND_BATCH_BYTES || output_capacity > MOBAGEN_WASM_MAX_COMMAND_BATCH_BYTES) {
      add_issue(result, WasmCommandChannelIssueCode::InvalidCapacity, WasmPluginExport::Allocate,
                "WASM command channel capacity exceeds the 16 MiB per-batch limit", MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
      return result;
    }

    std::uint64_t cursor = MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE;
    const auto input_bytes_relative = cursor;
    cursor = align_exchange(cursor + input_capacity);
    const auto output_batch_relative = cursor;
    cursor += MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE;
    const auto output_bytes_relative = cursor;
    cursor = align_exchange(cursor + output_capacity);
    const auto result_relative = cursor;
    cursor += MOBAGEN_WASM_COMMAND_RESULT_V1_SIZE;
    if (cursor > std::numeric_limits<std::uint32_t>::max()) {
      add_issue(result, WasmCommandChannelIssueCode::InvalidCapacity, WasmPluginExport::Allocate,
                "WASM command channel exchange size overflows 32-bit linear memory", MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
      return result;
    }
    const auto total_size = static_cast<std::uint32_t>(cursor);

    const std::array allocate_arguments{total_size, MOBAGEN_WASM_EXCHANGE_ALIGNMENT};
    auto allocated = invoke_portable_wasm(instance, WasmPluginExport::Allocate, allocate_arguments);
    if (!allocated.ok()) {
      add_issue(result, WasmCommandChannelIssueCode::BackendFailure, WasmPluginExport::Allocate,
                allocated.error.has_value() ? std::move(*allocated.error) : "WASM command exchange allocation failed", MOBAGEN_WASM_STATUS_FAILED);
      return result;
    }
    const auto base_offset = *allocated.value;
    if (base_offset == MOBAGEN_WASM_NULL_OFFSET) {
      add_issue(result, WasmCommandChannelIssueCode::AllocationFailed, WasmPluginExport::Allocate,
                "WASM guest could not allocate persistent command exchange memory", MOBAGEN_WASM_STATUS_OUT_OF_MEMORY);
      return result;
    }
    if (!exchange_is_valid(instance.memory(), base_offset, total_size)) {
      add_issue(result, WasmCommandChannelIssueCode::InvalidExchangeBuffer, WasmPluginExport::Allocate,
                "WASM guest allocator returned a misaligned or out-of-bounds command exchange", MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
      release_region(instance, base_offset, total_size, result);
      return result;
    }

    const auto absolute = [base_offset](std::uint64_t relative) { return static_cast<std::uint32_t>(base_offset + relative); };
    try {
      result.channel = std::unique_ptr<WasmCommandChannel>(
          new WasmCommandChannel(instance, base_offset, total_size, base_offset, absolute(input_bytes_relative), input_capacity,
                                 absolute(output_batch_relative), absolute(output_bytes_relative), output_capacity, absolute(result_relative)));
    } catch (const std::bad_alloc&) {
      add_issue(result, WasmCommandChannelIssueCode::OutOfMemory, WasmPluginExport::Allocate,
                "WASM command channel host allocation ran out of memory", MOBAGEN_WASM_STATUS_OUT_OF_MEMORY);
      release_region(instance, base_offset, total_size, result);
    }
    return result;
  }

}  // namespace mobagen::plugins
