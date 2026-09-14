#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "plugins/wasm_runtime.hpp"

namespace {

  void write_u32(std::vector<std::byte>& memory, std::size_t offset, std::uint32_t value) {
    REQUIRE(offset <= memory.size());
    REQUIRE(sizeof(value) <= memory.size() - offset);
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
      memory[offset + byte] = std::byte{static_cast<std::uint8_t>(value >> (byte * 8U))};
    }
  }

  void write_string(std::vector<std::byte>& memory, std::uint32_t offset, std::string_view value) {
    REQUIRE(offset <= memory.size());
    REQUIRE(value.size() <= memory.size() - offset);
    std::ranges::transform(value, memory.begin() + offset, [](char byte) { return std::byte{static_cast<std::uint8_t>(byte)}; });
  }

  struct Invocation {
    mobagen::plugins::WasmPluginExport function{};
    std::vector<std::uint32_t> arguments;
  };

  class FakeWasmInstance final : public mobagen::plugins::PortableWasmInstance {
  public:
    mobagen::plugins::WasmInvocationResult invoke(mobagen::plugins::WasmPluginExport function, std::span<const std::uint32_t> arguments) override {
      invocations.push_back({function, {arguments.begin(), arguments.end()}});
      if (function == mobagen::plugins::WasmPluginExport::Allocate) {
        if (allocate_traps) return mobagen::plugins::WasmInvocationResult::failure("missing allocate export");
        return mobagen::plugins::WasmInvocationResult::success(allocation_offset);
      }
      if (function == mobagen::plugins::WasmPluginExport::Query) {
        if (query_throws) throw std::runtime_error{"guest trapped"};
        if (query_status != MOBAGEN_WASM_STATUS_OK) return mobagen::plugins::WasmInvocationResult::success(query_status);
        encode_descriptor(arguments[0]);
        return mobagen::plugins::WasmInvocationResult::success(MOBAGEN_WASM_STATUS_OK);
      }
      if (function == mobagen::plugins::WasmPluginExport::Deallocate) {
        if (deallocate_traps) return mobagen::plugins::WasmInvocationResult::failure("deallocate trapped");
        return mobagen::plugins::WasmInvocationResult::success(deallocate_status);
      }
      return mobagen::plugins::WasmInvocationResult::failure("unexpected export");
    }

    std::span<const std::byte> memory() const noexcept override { return linear_memory; }
    std::span<std::byte> writable_memory() noexcept override { return linear_memory; }

    void encode_descriptor(std::uint32_t descriptor_offset) {
      constexpr std::uint32_t id_offset = 96;
      constexpr std::uint32_t capability_offset = 128;
      constexpr std::uint32_t provides_offset = 152;
      constexpr std::string_view id = "mobagen.wasm-ref";
      constexpr std::string_view capability = "runtime.tick.v1";
      write_string(linear_memory, id_offset, id);
      write_string(linear_memory, capability_offset, capability);
      write_u32(linear_memory, provides_offset, capability_offset);
      write_u32(linear_memory, provides_offset + 4, static_cast<std::uint32_t>(capability.size()));
      write_u32(linear_memory, descriptor_offset, malformed_descriptor ? 0 : MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
      write_u32(linear_memory, descriptor_offset + 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
      write_u32(linear_memory, descriptor_offset + 8, id_offset);
      write_u32(linear_memory, descriptor_offset + 12, static_cast<std::uint32_t>(id.size()));
      write_u32(linear_memory, descriptor_offset + 16, 1);
      write_u32(linear_memory, descriptor_offset + 20, 0);
      write_u32(linear_memory, descriptor_offset + 24, 0);
      write_u32(linear_memory, descriptor_offset + 28, MOBAGEN_WASM_RELOAD_RESTART);
      write_u32(linear_memory, descriptor_offset + 32, provides_offset);
      write_u32(linear_memory, descriptor_offset + 36, 1);
      for (std::uint32_t field = 40; field < MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE; field += 4) {
        write_u32(linear_memory, descriptor_offset + field, 0);
      }
    }

    std::vector<std::byte> linear_memory = std::vector<std::byte>(256);
    std::vector<Invocation> invocations;
    std::uint32_t allocation_offset{8};
    std::uint32_t query_status{MOBAGEN_WASM_STATUS_OK};
    std::uint32_t deallocate_status{MOBAGEN_WASM_STATUS_OK};
    bool allocate_traps{false};
    bool query_throws{false};
    bool deallocate_traps{false};
    bool malformed_descriptor{false};
  };

  bool has_issue(const mobagen::plugins::WasmPluginQueryResult& result, mobagen::plugins::WasmPluginQueryIssueCode code) {
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
  }

}  // namespace

TEST_CASE("WASM runtime: descriptor query owns metadata and releases guest scratch") {
  FakeWasmInstance instance;
  const auto result = mobagen::plugins::query_portable_wasm_plugin(instance);

  REQUIRE(result.provider.has_value());
  CHECK(result.provider->id == "mobagen.wasm-ref");
  REQUIRE(instance.invocations.size() == 3);
  CHECK(instance.invocations[0].function == mobagen::plugins::WasmPluginExport::Allocate);
  CHECK(instance.invocations[0].arguments == std::vector<std::uint32_t>{MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE, MOBAGEN_WASM_EXCHANGE_ALIGNMENT});
  CHECK(instance.invocations[1].function == mobagen::plugins::WasmPluginExport::Query);
  CHECK(instance.invocations[1].arguments == std::vector<std::uint32_t>{8, MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE});
  CHECK(instance.invocations[2].function == mobagen::plugins::WasmPluginExport::Deallocate);
  CHECK(instance.invocations[2].arguments == std::vector<std::uint32_t>{8, MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE, MOBAGEN_WASM_EXCHANGE_ALIGNMENT});
  instance.linear_memory[96] = std::byte{};
  CHECK(result.provider->id == "mobagen.wasm-ref");
}

TEST_CASE("WASM runtime: backend export identifiers match the public ABI") {
  using mobagen::plugins::wasm_plugin_export_name;
  using mobagen::plugins::WasmPluginExport;
  CHECK(std::string{wasm_plugin_export_name(WasmPluginExport::Allocate)} == MOBAGEN_WASM_EXPORT_ALLOCATE_V1);
  CHECK(std::string{wasm_plugin_export_name(WasmPluginExport::Deallocate)} == MOBAGEN_WASM_EXPORT_DEALLOCATE_V1);
  CHECK(std::string{wasm_plugin_export_name(WasmPluginExport::Query)} == MOBAGEN_WASM_EXPORT_QUERY_V1);
  CHECK(std::string{wasm_plugin_export_name(WasmPluginExport::Configure)} == MOBAGEN_WASM_EXPORT_CONFIGURE_V1);
  CHECK(std::string{wasm_plugin_export_name(WasmPluginExport::Start)} == MOBAGEN_WASM_EXPORT_START_V1);
  CHECK(std::string{wasm_plugin_export_name(WasmPluginExport::Quiesce)} == MOBAGEN_WASM_EXPORT_QUIESCE_V1);
  CHECK(std::string{wasm_plugin_export_name(WasmPluginExport::Stop)} == MOBAGEN_WASM_EXPORT_STOP_V1);
  CHECK(std::string{wasm_plugin_export_name(WasmPluginExport::Process)} == MOBAGEN_WASM_EXPORT_PROCESS_V1);
}

TEST_CASE("WASM runtime: allocation and exchange bounds fail before query") {
  FakeWasmInstance instance;
  instance.allocation_offset = MOBAGEN_WASM_NULL_OFFSET;
  const auto unavailable = mobagen::plugins::query_portable_wasm_plugin(instance);
  CHECK_FALSE(unavailable.provider.has_value());
  CHECK(has_issue(unavailable, mobagen::plugins::WasmPluginQueryIssueCode::AllocationFailed));
  CHECK(instance.invocations.size() == 1);

  FakeWasmInstance invalid;
  invalid.allocation_offset = 3;
  const auto misaligned = mobagen::plugins::query_portable_wasm_plugin(invalid);
  CHECK_FALSE(misaligned.provider.has_value());
  CHECK(has_issue(misaligned, mobagen::plugins::WasmPluginQueryIssueCode::InvalidExchangeBuffer));
  REQUIRE(invalid.invocations.size() == 2);
  CHECK(invalid.invocations.back().function == mobagen::plugins::WasmPluginExport::Deallocate);

  FakeWasmInstance trapped;
  trapped.allocate_traps = true;
  const auto backend_failure = mobagen::plugins::query_portable_wasm_plugin(trapped);
  CHECK_FALSE(backend_failure.provider.has_value());
  CHECK(has_issue(backend_failure, mobagen::plugins::WasmPluginQueryIssueCode::BackendFailure));
  CHECK(trapped.invocations.size() == 1);
}

TEST_CASE("WASM runtime: query traps callback failures and malformed contracts clean up") {
  FakeWasmInstance trapped;
  trapped.query_throws = true;
  const auto trap = mobagen::plugins::query_portable_wasm_plugin(trapped);
  CHECK_FALSE(trap.provider.has_value());
  CHECK(has_issue(trap, mobagen::plugins::WasmPluginQueryIssueCode::BackendFailure));
  CHECK(trapped.invocations.back().function == mobagen::plugins::WasmPluginExport::Deallocate);

  FakeWasmInstance callback_failure;
  callback_failure.query_status = MOBAGEN_WASM_STATUS_FAILED;
  const auto failed = mobagen::plugins::query_portable_wasm_plugin(callback_failure);
  CHECK_FALSE(failed.provider.has_value());
  CHECK(has_issue(failed, mobagen::plugins::WasmPluginQueryIssueCode::CallbackFailed));
  CHECK(callback_failure.invocations.back().function == mobagen::plugins::WasmPluginExport::Deallocate);

  FakeWasmInstance malformed;
  malformed.malformed_descriptor = true;
  const auto invalid = mobagen::plugins::query_portable_wasm_plugin(malformed);
  CHECK_FALSE(invalid.provider.has_value());
  CHECK(has_issue(invalid, mobagen::plugins::WasmPluginQueryIssueCode::ContractInvalid));
  CHECK(malformed.invocations.back().function == mobagen::plugins::WasmPluginExport::Deallocate);
}

TEST_CASE("WASM runtime: deallocation failure invalidates an otherwise valid query") {
  FakeWasmInstance instance;
  instance.deallocate_status = MOBAGEN_WASM_STATUS_FAILED;
  const auto result = mobagen::plugins::query_portable_wasm_plugin(instance);

  CHECK_FALSE(result.provider.has_value());
  CHECK(has_issue(result, mobagen::plugins::WasmPluginQueryIssueCode::DeallocationFailed));

  FakeWasmInstance trapped;
  trapped.deallocate_traps = true;
  const auto backend_failure = mobagen::plugins::query_portable_wasm_plugin(trapped);
  CHECK_FALSE(backend_failure.provider.has_value());
  CHECK(has_issue(backend_failure, mobagen::plugins::WasmPluginQueryIssueCode::DeallocationFailed));
}
