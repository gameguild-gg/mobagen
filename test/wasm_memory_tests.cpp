#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "plugins/wasm_memory.hpp"

namespace {

  void write_u32(std::vector<std::byte>& memory, std::size_t offset, std::uint32_t value) {
    REQUIRE(offset <= memory.size());
    REQUIRE(sizeof(value) <= memory.size() - offset);
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
      memory[offset + byte] = std::byte{static_cast<std::uint8_t>(value >> (byte * 8U))};
    }
  }

  std::vector<std::byte> valid_batch() {
    std::vector<std::byte> memory(48);
    write_u32(memory, 0, MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE);
    write_u32(memory, 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
    write_u32(memory, 8, 24);
    write_u32(memory, 12, 24);
    write_u32(memory, 16, 2);
    write_u32(memory, 20, 0);
    write_u32(memory, 24, 8);
    write_u32(memory, 28, 7);
    write_u32(memory, 32, 16);
    write_u32(memory, 36, 9);
    return memory;
  }

  bool has_issue(const mobagen::plugins::WasmCommandBatchValidationResult& result, mobagen::plugins::WasmMemoryIssueCode code) {
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
  }

}  // namespace

TEST_CASE("WASM memory: a bounded aligned command batch validates without copying") {
  const auto memory = valid_batch();

  const auto result = mobagen::plugins::validate_wasm_command_batch(memory, 0);

  REQUIRE(result.ok());
  CHECK(result.batch->command_count == 2);
  CHECK(result.batch->bytes.size() == 24);
  CHECK(result.batch->bytes.data() == memory.data() + 24);
}

TEST_CASE("WASM memory: wrapped and out-of-bounds spans are rejected") {
  auto memory = valid_batch();
  write_u32(memory, 8, 0xfffffff8U);
  write_u32(memory, 12, 16);

  const auto wrapped = mobagen::plugins::validate_wasm_command_batch(memory, 0);
  const auto missing_header = mobagen::plugins::validate_wasm_command_batch(memory, static_cast<std::uint32_t>(memory.size() - 4));

  CHECK_FALSE(wrapped.ok());
  CHECK(has_issue(wrapped, mobagen::plugins::WasmMemoryIssueCode::OutOfBounds));
  CHECK_FALSE(missing_header.ok());
  CHECK(has_issue(missing_header, mobagen::plugins::WasmMemoryIssueCode::OutOfBounds));
}

TEST_CASE("WASM memory: malformed command framing fails closed") {
  SUBCASE("command size is smaller than its header") {
    auto memory = valid_batch();
    write_u32(memory, 24, 4);
    const auto result = mobagen::plugins::validate_wasm_command_batch(memory, 0);
    CHECK_FALSE(result.ok());
    CHECK(has_issue(result, mobagen::plugins::WasmMemoryIssueCode::InvalidCommandSize));
  }

  SUBCASE("command size must preserve batch alignment") {
    auto memory = valid_batch();
    write_u32(memory, 24, 12);
    const auto result = mobagen::plugins::validate_wasm_command_batch(memory, 0);
    CHECK_FALSE(result.ok());
    CHECK(has_issue(result, mobagen::plugins::WasmMemoryIssueCode::Misaligned));
  }

  SUBCASE("declared command count must consume the batch exactly") {
    auto memory = valid_batch();
    write_u32(memory, 16, 1);
    const auto result = mobagen::plugins::validate_wasm_command_batch(memory, 0);
    CHECK_FALSE(result.ok());
    CHECK(has_issue(result, mobagen::plugins::WasmMemoryIssueCode::TrailingBytes));
  }

  SUBCASE("command parsing never reads beyond the declared batch") {
    auto memory = valid_batch();
    memory.resize(64);
    write_u32(memory, 24, 24);
    const auto result = mobagen::plugins::validate_wasm_command_batch(memory, 0);
    CHECK_FALSE(result.ok());
    CHECK(has_issue(result, mobagen::plugins::WasmMemoryIssueCode::InvalidCommandCount));
  }
}

TEST_CASE("WASM memory: ABI metadata and declared limits fail closed") {
  SUBCASE("batch header must be aligned") {
    const auto memory = valid_batch();
    const auto result = mobagen::plugins::validate_wasm_command_batch(memory, 2);
    CHECK_FALSE(result.ok());
    CHECK(has_issue(result, mobagen::plugins::WasmMemoryIssueCode::Misaligned));
  }

  SUBCASE("batch ABI version must match") {
    auto memory = valid_batch();
    write_u32(memory, 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION + 1);
    const auto result = mobagen::plugins::validate_wasm_command_batch(memory, 0);
    CHECK_FALSE(result.ok());
    CHECK(has_issue(result, mobagen::plugins::WasmMemoryIssueCode::InvalidStruct));
  }

  SUBCASE("reserved fields must remain zero") {
    auto memory = valid_batch();
    write_u32(memory, 20, 1);
    const auto result = mobagen::plugins::validate_wasm_command_batch(memory, 0);
    CHECK_FALSE(result.ok());
    CHECK(has_issue(result, mobagen::plugins::WasmMemoryIssueCode::InvalidStruct));
  }

  SUBCASE("declared bytes cannot exceed the hard batch limit") {
    auto memory = valid_batch();
    write_u32(memory, 12, MOBAGEN_WASM_MAX_COMMAND_BATCH_BYTES + 1);
    const auto result = mobagen::plugins::validate_wasm_command_batch(memory, 0);
    CHECK_FALSE(result.ok());
    CHECK(has_issue(result, mobagen::plugins::WasmMemoryIssueCode::SizeLimit));
  }

  SUBCASE("declared command count cannot exceed its bounded payload") {
    auto memory = valid_batch();
    write_u32(memory, 16, 4);
    const auto result = mobagen::plugins::validate_wasm_command_batch(memory, 0);
    CHECK_FALSE(result.ok());
    CHECK(has_issue(result, mobagen::plugins::WasmMemoryIssueCode::InvalidCommandCount));
  }
}
