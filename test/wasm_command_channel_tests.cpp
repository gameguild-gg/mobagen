#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <vector>

#include "plugins/wasm_command_channel.hpp"

namespace {

  std::uint32_t read_u32(std::span<const std::byte> memory, std::size_t offset) {
    if (offset > memory.size() || sizeof(std::uint32_t) > memory.size() - offset) return 0;
    std::uint32_t value = 0;
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
      value |= std::to_integer<std::uint32_t>(memory[offset + byte]) << (byte * 8U);
    }
    return value;
  }

  void write_u32(std::span<std::byte> memory, std::size_t offset, std::uint32_t value) {
    if (offset > memory.size() || sizeof(value) > memory.size() - offset) return;
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
      memory[offset + byte] = std::byte{static_cast<std::uint8_t>(value >> (byte * 8U))};
    }
  }

  class FakeCommandInstance final : public mobagen::plugins::PortableWasmInstance {
  public:
    mobagen::plugins::WasmInvocationResult invoke(mobagen::plugins::WasmPluginExport function, std::span<const std::uint32_t> arguments) override {
      if (function == mobagen::plugins::WasmPluginExport::Allocate) {
        ++allocate_calls;
        return mobagen::plugins::WasmInvocationResult::success(allocation_offset);
      }
      if (function == mobagen::plugins::WasmPluginExport::Deallocate) {
        ++deallocate_calls;
        return mobagen::plugins::WasmInvocationResult::success(deallocate_status);
      }
      if (function != mobagen::plugins::WasmPluginExport::Process || arguments.size() != 3) {
        return mobagen::plugins::WasmInvocationResult::failure("unexpected export");
      }

      ++process_calls;
      if (process_traps) return mobagen::plugins::WasmInvocationResult::failure("process trapped");
      if (process_status != MOBAGEN_WASM_STATUS_OK) return mobagen::plugins::WasmInvocationResult::success(process_status);

      const auto input_batch = arguments[0];
      const auto output_batch = arguments[1];
      const auto result_offset = arguments[2];
      const auto input_bytes_offset = read_u32(linear_memory, input_batch + 8);
      const auto input_bytes_size = read_u32(linear_memory, input_batch + 12);
      const auto input_count = read_u32(linear_memory, input_batch + 16);
      const auto output_bytes_offset = read_u32(linear_memory, output_batch + 8);
      const auto output_capacity = read_u32(linear_memory, output_batch + 12);
      last_output_bytes_offset = output_bytes_offset;
      received.assign(linear_memory.begin() + input_bytes_offset, linear_memory.begin() + input_bytes_offset + input_bytes_size);
      std::ranges::copy(received, linear_memory.begin() + output_bytes_offset);

      const auto bytes_written = oversized_result ? output_capacity + 1 : input_bytes_size;
      write_u32(linear_memory, output_batch + 8, corrupt_output_offset ? output_bytes_offset + 8 : output_bytes_offset);
      write_u32(linear_memory, output_batch + 12, bytes_written);
      write_u32(linear_memory, output_batch + 16, input_count);
      write_u32(linear_memory, result_offset, MOBAGEN_WASM_COMMAND_RESULT_V1_SIZE);
      write_u32(linear_memory, result_offset + 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
      write_u32(linear_memory, result_offset + 8, result_status);
      write_u32(linear_memory, result_offset + 12, bytes_written);
      write_u32(linear_memory, result_offset + 16, input_count);
      write_u32(linear_memory, result_offset + 20, 0);
      return mobagen::plugins::WasmInvocationResult::success(MOBAGEN_WASM_STATUS_OK);
    }

    std::span<const std::byte> memory() const noexcept override { return linear_memory; }
    std::span<std::byte> writable_memory() noexcept override { return linear_memory; }

    std::vector<std::byte> linear_memory = std::vector<std::byte>(4096);
    std::vector<std::byte> received;
    std::uint32_t allocation_offset{8};
    std::uint32_t deallocate_status{MOBAGEN_WASM_STATUS_OK};
    std::uint32_t process_status{MOBAGEN_WASM_STATUS_OK};
    std::uint32_t result_status{MOBAGEN_WASM_STATUS_OK};
    std::uint32_t last_output_bytes_offset{};
    std::uint32_t allocate_calls{};
    std::uint32_t deallocate_calls{};
    std::uint32_t process_calls{};
    bool process_traps{false};
    bool corrupt_output_offset{false};
    bool oversized_result{false};
  };

  std::array<std::byte, 8> one_command(std::uint32_t opcode = 7) {
    std::array<std::byte, 8> command{};
    write_u32(command, 0, static_cast<std::uint32_t>(command.size()));
    write_u32(command, 4, opcode);
    return command;
  }

  template <typename Result> bool has_issue(const Result& result, mobagen::plugins::WasmCommandChannelIssueCode code) {
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
  }

}  // namespace

TEST_CASE("WASM command channel: persistent exchange processes zero-copy batches") {
  FakeCommandInstance instance;
  auto created = mobagen::plugins::create_wasm_command_channel(instance, 64, 64);
  REQUIRE(created.channel != nullptr);
  CHECK(instance.allocate_calls == 1);

  const auto command = one_command();
  const auto first = created.channel->process(command, 1);
  REQUIRE(first.output.has_value());
  CHECK(first.issues.empty());
  CHECK(first.output->command_count == 1);
  CHECK(first.output->bytes.size() == command.size());
  CHECK(first.output->bytes.data() == instance.linear_memory.data() + instance.last_output_bytes_offset);
  CHECK(instance.received == std::vector<std::byte>{command.begin(), command.end()});

  const auto second = created.channel->process(one_command(9), 1);
  REQUIRE(second.output.has_value());
  CHECK(instance.allocate_calls == 1);
  CHECK(instance.deallocate_calls == 0);
  CHECK(instance.process_calls == 2);

  CHECK(created.channel->close().ok());
  CHECK(instance.deallocate_calls == 1);
  CHECK(created.channel->close().ok());
  CHECK(instance.deallocate_calls == 1);
}

TEST_CASE("WASM command channel: invalid input never crosses the sandbox boundary") {
  FakeCommandInstance instance;
  auto created = mobagen::plugins::create_wasm_command_channel(instance, 64, 64);
  REQUIRE(created.channel != nullptr);
  auto command = one_command();
  write_u32(command, 0, 4);

  const auto malformed = created.channel->process(command, 1);

  CHECK_FALSE(malformed.output.has_value());
  CHECK(has_issue(malformed, mobagen::plugins::WasmCommandChannelIssueCode::InvalidInputBatch));
  CHECK(instance.process_calls == 0);
}

TEST_CASE("WASM command channel: guest result and output mutations fail closed") {
  SUBCASE("output span cannot escape its reserved region") {
    FakeCommandInstance instance;
    instance.corrupt_output_offset = true;
    auto created = mobagen::plugins::create_wasm_command_channel(instance, 64, 64);
    REQUIRE(created.channel != nullptr);
    const auto result = created.channel->process(one_command(), 1);
    CHECK_FALSE(result.output.has_value());
    CHECK(has_issue(result, mobagen::plugins::WasmCommandChannelIssueCode::InvalidOutputBatch));
  }

  SUBCASE("reported output cannot exceed capacity") {
    FakeCommandInstance instance;
    instance.oversized_result = true;
    auto created = mobagen::plugins::create_wasm_command_channel(instance, 64, 64);
    REQUIRE(created.channel != nullptr);
    const auto result = created.channel->process(one_command(), 1);
    CHECK_FALSE(result.output.has_value());
    CHECK(has_issue(result, mobagen::plugins::WasmCommandChannelIssueCode::InvalidResult));
  }

  SUBCASE("backend traps remain structured failures") {
    FakeCommandInstance instance;
    instance.process_traps = true;
    auto created = mobagen::plugins::create_wasm_command_channel(instance, 64, 64);
    REQUIRE(created.channel != nullptr);
    const auto result = created.channel->process(one_command(), 1);
    CHECK_FALSE(result.output.has_value());
    CHECK(has_issue(result, mobagen::plugins::WasmCommandChannelIssueCode::BackendFailure));
  }
}

TEST_CASE("WASM command channel: creation rejects hostile capacities and allocator offsets") {
  FakeCommandInstance oversized_instance;
  const auto oversized = mobagen::plugins::create_wasm_command_channel(oversized_instance, MOBAGEN_WASM_MAX_COMMAND_BATCH_BYTES + 1, 64);
  CHECK_FALSE(oversized.channel);
  CHECK(has_issue(oversized, mobagen::plugins::WasmCommandChannelIssueCode::InvalidCapacity));
  CHECK(oversized_instance.allocate_calls == 0);

  FakeCommandInstance misaligned_instance;
  misaligned_instance.allocation_offset = 3;
  const auto misaligned = mobagen::plugins::create_wasm_command_channel(misaligned_instance, 64, 64);
  CHECK_FALSE(misaligned.channel);
  CHECK(has_issue(misaligned, mobagen::plugins::WasmCommandChannelIssueCode::InvalidExchangeBuffer));
  CHECK(misaligned_instance.deallocate_calls == 1);
}

TEST_CASE("WASM command channel: process enforces owner thread and closed state") {
  FakeCommandInstance instance;
  auto created = mobagen::plugins::create_wasm_command_channel(instance, 64, 64);
  REQUIRE(created.channel != nullptr);
  const auto command = one_command();
  mobagen::plugins::WasmCommandProcessResult foreign;
  std::thread worker([&] { foreign = created.channel->process(command, 1); });
  worker.join();
  CHECK(has_issue(foreign, mobagen::plugins::WasmCommandChannelIssueCode::WrongThread));
  CHECK(instance.process_calls == 0);

  CHECK(created.channel->close().ok());
  const auto closed = created.channel->process(command, 1);
  CHECK(has_issue(closed, mobagen::plugins::WasmCommandChannelIssueCode::Closed));
}
