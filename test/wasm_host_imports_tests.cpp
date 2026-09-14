#include <doctest/doctest.h>

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "plugins/wasm_host_imports.hpp"
#include "support/wasm_plugin_test_support.hpp"

namespace module_allocation_probe {
  extern std::atomic_bool enabled;
  extern std::atomic_size_t count;
}  // namespace module_allocation_probe

namespace {

  std::uint32_t read_u32(std::span<const std::byte> memory, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
      value |= std::to_integer<std::uint32_t>(memory[offset + byte]) << (byte * 8U);
    }
    return value;
  }

  struct HostCapture {
    std::uint32_t log_level{};
    std::string log_message;
    std::uint32_t command_count{};
    std::vector<std::string> permissions;
    std::size_t log_calls{};
    std::size_t submit_calls{};
  };

  std::uint32_t capture_log(void* state, std::uint32_t level, std::string_view message) noexcept {
    auto& capture = *static_cast<HostCapture*>(state);
    capture.log_level = level;
    capture.log_message = message;
    ++capture.log_calls;
    return MOBAGEN_WASM_STATUS_OK;
  }

  std::uint32_t capture_commands(void* state, mobagen::plugins::WasmCommandBatchView batch,
                                 std::span<const std::string> permissions) noexcept {
    auto& capture = *static_cast<HostCapture*>(state);
    capture.command_count = batch.command_count;
    capture.permissions.assign(permissions.begin(), permissions.end());
    ++capture.submit_calls;
    return MOBAGEN_WASM_STATUS_OK;
  }

  std::uint32_t count_log(void* state, std::uint32_t, std::string_view) noexcept {
    ++*static_cast<std::size_t*>(state);
    return MOBAGEN_WASM_STATUS_OK;
  }

  std::uint32_t count_commands(void* state, mobagen::plugins::WasmCommandBatchView,
                               std::span<const std::string>) noexcept {
    ++*static_cast<std::size_t*>(state);
    return MOBAGEN_WASM_STATUS_OK;
  }

  mobagen::modules::CapabilityRegistry registry() {
    mobagen::modules::CapabilityRegistryBuilder builder;
    builder.add({
        .id = "mobagen.host",
        .version = {1, 0, 0},
        .provides = {"render.backend.v1"},
        .targets = {mobagen::modules::TargetPlatform::Windows},
        .linkages = {mobagen::modules::LinkageMode::Static},
    });
    auto result = builder.build();
    REQUIRE(result.ok());
    return std::move(*result.registry);
  }

}  // namespace

TEST_CASE("WASM host imports: logging validates borrowed guest text") {
  using namespace mobagen::plugins;
  HostCapture capture;
  WasmHostImports imports{{.state = &capture, .log = capture_log, .submit_commands = capture_commands}};
  std::vector<std::byte> memory(64);
  constexpr std::string_view message = "portable plugin ready";
  mobagen::test::write_string(memory, 8, message);

  CHECK(imports.log(memory, 3, 8, static_cast<std::uint32_t>(message.size())) == MOBAGEN_WASM_STATUS_OK);
  CHECK(capture.log_calls == 1);
  CHECK(capture.log_level == 3);
  CHECK(capture.log_message == message);

  CHECK(imports.log(memory, 3, 60, 8) == MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
  CHECK(imports.log(memory, 3, 0, MOBAGEN_WASM_MAX_STRING_BYTES + 1U) == MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
  CHECK(capture.log_calls == 1);
}

TEST_CASE("WASM host imports: capability lookup returns a bounded generational handle") {
  using namespace mobagen::plugins;
  auto capabilities = registry();
  WasmHostImports imports;
  REQUIRE(imports.bind(capabilities, {}));
  std::vector<std::byte> memory(256);
  constexpr std::string_view capability = "render.backend.v1";
  mobagen::test::write_string(memory, 32, capability);

  CHECK(imports.find_capability(memory, 32, static_cast<std::uint32_t>(capability.size()), 1, 128) == MOBAGEN_WASM_STATUS_OK);
  const auto index = capabilities.find_capability(capability);
  REQUIRE(index.has_value());
  CHECK(read_u32(memory, 128) == index->value);
  CHECK(read_u32(memory, 132) == capabilities.generation().value);

  CHECK(imports.find_capability(memory, 32, static_cast<std::uint32_t>(capability.size()), 2, 128) == MOBAGEN_WASM_STATUS_NOT_FOUND);
  CHECK(read_u32(memory, 128) == 0);
  CHECK(read_u32(memory, 132) == 0);
  CHECK(imports.find_capability(memory, 250, 16, 1, 128) == MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
  CHECK(imports.find_capability(memory, 32, static_cast<std::uint32_t>(capability.size()), 1, 253)
        == MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
}

TEST_CASE("WASM host imports: command submission validates the whole batch before dispatch") {
  using namespace mobagen::plugins;
  HostCapture capture;
  auto capabilities = registry();
  WasmHostImports imports{{.state = &capture, .log = capture_log, .submit_commands = capture_commands}};
  const std::vector<std::string> permissions{"gpu", "debug"};
  REQUIRE(imports.bind(capabilities, permissions));
  std::vector<std::byte> memory(256);
  constexpr std::uint32_t batch_offset = 8;
  constexpr std::uint32_t bytes_offset = 64;
  constexpr std::uint32_t result_offset = 128;
  mobagen::test::write_u32(memory, batch_offset, MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE);
  mobagen::test::write_u32(memory, batch_offset + 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
  mobagen::test::write_u32(memory, batch_offset + 8, bytes_offset);
  mobagen::test::write_u32(memory, batch_offset + 12, MOBAGEN_WASM_COMMAND_HEADER_V1_SIZE);
  mobagen::test::write_u32(memory, batch_offset + 16, 1);
  mobagen::test::write_u32(memory, batch_offset + 20, 0);
  mobagen::test::write_u32(memory, bytes_offset, MOBAGEN_WASM_COMMAND_HEADER_V1_SIZE);
  mobagen::test::write_u32(memory, bytes_offset + 4, 42);

  CHECK(imports.submit_commands(memory, batch_offset, result_offset) == MOBAGEN_WASM_STATUS_OK);
  CHECK(capture.submit_calls == 1);
  CHECK(capture.command_count == 1);
  CHECK(capture.permissions == std::vector<std::string>{"debug", "gpu"});
  CHECK(read_u32(memory, result_offset) == MOBAGEN_WASM_COMMAND_RESULT_V1_SIZE);
  CHECK(read_u32(memory, result_offset + 4) == MOBAGEN_WASM_PLUGIN_ABI_VERSION);
  CHECK(read_u32(memory, result_offset + 8) == MOBAGEN_WASM_STATUS_OK);

  mobagen::test::write_u32(memory, bytes_offset, MOBAGEN_WASM_COMMAND_HEADER_V1_SIZE - 1U);
  CHECK(imports.submit_commands(memory, batch_offset, result_offset) == MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
  CHECK(capture.submit_calls == 1);
  CHECK(read_u32(memory, result_offset + 8) == MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);

  mobagen::test::write_u32(memory, bytes_offset, MOBAGEN_WASM_COMMAND_HEADER_V1_SIZE);
  CHECK(imports.submit_commands(memory, batch_offset, bytes_offset) == MOBAGEN_WASM_STATUS_INVALID_ARGUMENT);
  CHECK(capture.submit_calls == 1);
}

TEST_CASE("WASM host imports: unbound and cross-thread control-plane calls are denied") {
  using namespace mobagen::plugins;
  auto capabilities = registry();
  WasmHostImports imports;
  std::vector<std::byte> memory(128);
  constexpr std::string_view capability = "render.backend.v1";
  mobagen::test::write_string(memory, 16, capability);

  CHECK(imports.find_capability(memory, 16, static_cast<std::uint32_t>(capability.size()), 1, 64) == MOBAGEN_WASM_STATUS_UNSUPPORTED);
  REQUIRE(imports.bind(capabilities, {}));
  const std::array invalid_permissions{std::string{"gpu"}, std::string{"gpu"}};
  CHECK_FALSE(imports.bind(capabilities, invalid_permissions));
  CHECK(imports.permissions().empty());
  std::atomic_uint32_t status{MOBAGEN_WASM_STATUS_OK};
  std::thread other([&] {
    status.store(imports.find_capability(memory, 16, static_cast<std::uint32_t>(capability.size()), 1, 64), std::memory_order_relaxed);
  });
  other.join();
  CHECK(status.load(std::memory_order_relaxed) == MOBAGEN_WASM_STATUS_FAILED);
}

TEST_CASE("WASM host imports: warmed successful dispatch performs zero allocations") {
  using namespace mobagen::plugins;
  auto capabilities = registry();
  std::size_t calls = 0;
  WasmHostImports imports{{.state = &calls, .log = count_log, .submit_commands = count_commands}};
  const std::vector<std::string> permissions{"gpu"};
  REQUIRE(imports.bind(capabilities, permissions));
  std::vector<std::byte> memory(256);
  constexpr std::string_view capability = "render.backend.v1";
  mobagen::test::write_string(memory, 32, capability);
  constexpr std::uint32_t batch_offset = 64;
  constexpr std::uint32_t bytes_offset = 96;
  constexpr std::uint32_t result_offset = 128;
  mobagen::test::write_u32(memory, batch_offset, MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE);
  mobagen::test::write_u32(memory, batch_offset + 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
  mobagen::test::write_u32(memory, batch_offset + 8, bytes_offset);
  mobagen::test::write_u32(memory, batch_offset + 12, MOBAGEN_WASM_COMMAND_HEADER_V1_SIZE);
  mobagen::test::write_u32(memory, batch_offset + 16, 1);
  mobagen::test::write_u32(memory, batch_offset + 20, 0);
  mobagen::test::write_u32(memory, bytes_offset, MOBAGEN_WASM_COMMAND_HEADER_V1_SIZE);
  mobagen::test::write_u32(memory, bytes_offset + 4, 7);
  REQUIRE(imports.log(memory, 2, 32, static_cast<std::uint32_t>(capability.size())) == MOBAGEN_WASM_STATUS_OK);
  REQUIRE(imports.find_capability(memory, 32, static_cast<std::uint32_t>(capability.size()), 1, 160) == MOBAGEN_WASM_STATUS_OK);
  REQUIRE(imports.submit_commands(memory, batch_offset, result_offset) == MOBAGEN_WASM_STATUS_OK);

  module_allocation_probe::count.store(0, std::memory_order_relaxed);
  module_allocation_probe::enabled.store(true, std::memory_order_release);
  const auto log_status = imports.log(memory, 2, 32, static_cast<std::uint32_t>(capability.size()));
  const auto find_status = imports.find_capability(memory, 32, static_cast<std::uint32_t>(capability.size()), 1, 160);
  const auto submit_status = imports.submit_commands(memory, batch_offset, result_offset);
  module_allocation_probe::enabled.store(false, std::memory_order_release);

  CHECK(log_status == MOBAGEN_WASM_STATUS_OK);
  CHECK(find_status == MOBAGEN_WASM_STATUS_OK);
  CHECK(submit_status == MOBAGEN_WASM_STATUS_OK);
  CHECK(module_allocation_probe::count.load(std::memory_order_relaxed) == 0);
}
