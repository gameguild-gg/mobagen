#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <mobagen/plugin/wasm_abi.h>

#include "modules/capability_registry.hpp"
#include "plugins/wamr_backend.hpp"
#include "plugins/wasm_runtime.hpp"

namespace {

  void append_u32_leb(std::vector<std::byte>& output, std::uint32_t value) {
    do {
      auto octet = static_cast<std::uint8_t>(value & 0x7fU);
      value >>= 7U;
      if (value != 0U) octet |= 0x80U;
      output.push_back(static_cast<std::byte>(octet));
    } while (value != 0U);
  }

  void append_i32_const(std::vector<std::byte>& output, std::int32_t value) {
    output.push_back(std::byte{0x41});
    bool more = true;
    while (more) {
      auto octet = static_cast<std::uint8_t>(value & 0x7f);
      value >>= 7;
      const bool sign_bit = (octet & 0x40U) != 0U;
      more = !((value == 0 && !sign_bit) || (value == -1 && sign_bit));
      if (more) octet |= 0x80U;
      output.push_back(static_cast<std::byte>(octet));
    }
  }

  void append_name(std::vector<std::byte>& output, std::string_view value) {
    append_u32_leb(output, static_cast<std::uint32_t>(value.size()));
    for (const char character : value) output.push_back(static_cast<std::byte>(character));
  }

  void append_section(std::vector<std::byte>& module, std::uint8_t id, std::span<const std::byte> contents) {
    module.push_back(static_cast<std::byte>(id));
    append_u32_leb(module, static_cast<std::uint32_t>(contents.size()));
    module.insert(module.end(), contents.begin(), contents.end());
  }

  std::vector<std::byte> make_module(std::string_view function_name, bool traps = false) {
    std::vector<std::byte> module{
        std::byte{0x00}, std::byte{0x61}, std::byte{0x73}, std::byte{0x6d},
        std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };

    const std::array type_section{
        std::byte{0x01},  // one type
        std::byte{0x60},  // function
        std::byte{0x00},  // no parameters
        std::byte{0x01},  // one result
        std::byte{0x7f},  // i32
    };
    append_section(module, 1, type_section);

    const std::array function_section{std::byte{0x01}, std::byte{0x00}};
    append_section(module, 3, function_section);

    const std::array memory_section{std::byte{0x01}, std::byte{0x00}, std::byte{0x01}};
    append_section(module, 5, memory_section);

    std::vector<std::byte> export_section;
    append_u32_leb(export_section, 2);
    append_name(export_section, "memory");
    export_section.push_back(std::byte{0x02});
    export_section.push_back(std::byte{0x00});
    append_name(export_section, function_name);
    export_section.push_back(std::byte{0x00});
    export_section.push_back(std::byte{0x00});
    append_section(module, 7, export_section);

    const std::array returning_body{std::byte{0x00}, std::byte{0x41}, std::byte{0x00}, std::byte{0x0b}};
    const std::array trapping_body{std::byte{0x00}, std::byte{0x00}, std::byte{0x0b}};
    const auto body = traps ? std::span<const std::byte>{trapping_body} : std::span<const std::byte>{returning_body};
    std::vector<std::byte> code_section;
    append_u32_leb(code_section, 1);
    append_u32_leb(code_section, static_cast<std::uint32_t>(body.size()));
    code_section.insert(code_section.end(), body.begin(), body.end());
    append_section(module, 10, code_section);

    return module;
  }

  void append_function_body(std::vector<std::byte>& code_section, std::span<const std::byte> body) {
    append_u32_leb(code_section, static_cast<std::uint32_t>(body.size()));
    code_section.insert(code_section.end(), body.begin(), body.end());
  }

  std::vector<std::byte> make_host_import_module(std::uint32_t message_size, std::uint32_t capability_size) {
    std::vector<std::byte> module{
        std::byte{0x00}, std::byte{0x61}, std::byte{0x73}, std::byte{0x6d},
        std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };

    std::vector<std::byte> types;
    append_u32_leb(types, 4);
    for (const std::uint32_t parameter_count : {3U, 4U, 2U, 0U}) {
      types.push_back(std::byte{0x60});
      append_u32_leb(types, parameter_count);
      for (std::uint32_t parameter = 0; parameter < parameter_count; ++parameter) types.push_back(std::byte{0x7f});
      types.push_back(std::byte{0x01});
      types.push_back(std::byte{0x7f});
    }
    append_section(module, 1, types);

    std::vector<std::byte> imports;
    append_u32_leb(imports, 3);
    for (const auto& [name, type] : std::array{
             std::pair<std::string_view, std::uint32_t>{MOBAGEN_WASM_IMPORT_LOG_V1, 0},
             std::pair<std::string_view, std::uint32_t>{MOBAGEN_WASM_IMPORT_FIND_CAPABILITY_V1, 1},
             std::pair<std::string_view, std::uint32_t>{MOBAGEN_WASM_IMPORT_SUBMIT_COMMANDS_V1, 2},
         }) {
      append_name(imports, MOBAGEN_WASM_IMPORT_MODULE_V1);
      append_name(imports, name);
      imports.push_back(std::byte{0x00});
      append_u32_leb(imports, type);
    }
    append_section(module, 2, imports);

    const std::array function_section{std::byte{0x03}, std::byte{0x03}, std::byte{0x03}, std::byte{0x03}};
    append_section(module, 3, function_section);
    const std::array memory_section{std::byte{0x01}, std::byte{0x00}, std::byte{0x01}};
    append_section(module, 5, memory_section);

    std::vector<std::byte> exports;
    append_u32_leb(exports, 4);
    append_name(exports, "memory");
    exports.insert(exports.end(), {std::byte{0x02}, std::byte{0x00}});
    for (const auto& [name, index] : std::array{
             std::pair<std::string_view, std::uint32_t>{MOBAGEN_WASM_EXPORT_START_V1, 3},
             std::pair<std::string_view, std::uint32_t>{MOBAGEN_WASM_EXPORT_QUERY_V1, 4},
             std::pair<std::string_view, std::uint32_t>{MOBAGEN_WASM_EXPORT_PROCESS_V1, 5},
         }) {
      append_name(exports, name);
      exports.push_back(std::byte{0x00});
      append_u32_leb(exports, index);
    }
    append_section(module, 7, exports);

    std::vector<std::byte> code;
    append_u32_leb(code, 3);
    std::vector<std::byte> log_body{std::byte{0x00}};
    append_i32_const(log_body, 2);
    append_i32_const(log_body, 32);
    append_i32_const(log_body, static_cast<std::int32_t>(message_size));
    log_body.insert(log_body.end(), {std::byte{0x10}, std::byte{0x00}, std::byte{0x0b}});
    append_function_body(code, log_body);

    std::vector<std::byte> find_body{std::byte{0x00}};
    append_i32_const(find_body, 64);
    append_i32_const(find_body, static_cast<std::int32_t>(capability_size));
    append_i32_const(find_body, 1);
    append_i32_const(find_body, 96);
    find_body.insert(find_body.end(), {std::byte{0x10}, std::byte{0x01}, std::byte{0x0b}});
    append_function_body(code, find_body);

    std::vector<std::byte> submit_body{std::byte{0x00}};
    append_i32_const(submit_body, 128);
    append_i32_const(submit_body, 200);
    submit_body.insert(submit_body.end(), {std::byte{0x10}, std::byte{0x02}, std::byte{0x0b}});
    append_function_body(code, submit_body);
    append_section(module, 10, code);

    return module;
  }

  void write_u32(std::span<std::byte> memory, std::size_t offset, std::uint32_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
      memory[offset + byte] = std::byte{static_cast<std::uint8_t>(value >> (byte * 8U))};
    }
  }

  std::uint32_t read_u32(std::span<const std::byte> memory, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
      value |= std::to_integer<std::uint32_t>(memory[offset + byte]) << (byte * 8U);
    }
    return value;
  }

  void write_text(std::span<std::byte> memory, std::size_t offset, std::string_view value) {
    std::ranges::transform(value, memory.begin() + static_cast<std::ptrdiff_t>(offset),
                           [](char character) { return static_cast<std::byte>(character); });
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

  std::shared_ptr<const mobagen::modules::CapabilityRegistry> make_registry() {
    mobagen::modules::CapabilityRegistryBuilder builder;
    builder.add({
        .id = "mobagen.host",
        .version = {1, 0, 0},
        .provides = {"render.backend.v1"},
        .targets = {mobagen::modules::TargetPlatform::Windows},
        .linkages = {mobagen::modules::LinkageMode::Static},
    });
    auto built = builder.build();
    REQUIRE(built.ok());
    return std::make_shared<const mobagen::modules::CapabilityRegistry>(std::move(*built.registry));
  }

}  // namespace

TEST_CASE("WAMR backend: instance owns bytecode and outlives its backend") {
  auto bytecode = make_module(mobagen::plugins::wasm_plugin_export_name(mobagen::plugins::WasmPluginExport::Start));
  std::unique_ptr<mobagen::plugins::PortableWasmInstance> instance;

  {
    mobagen::plugins::WamrBackend backend;
    REQUIRE(backend.available());
    auto instantiated = backend.instantiate(bytecode, nullptr);
    REQUIRE_MESSAGE(instantiated.ok(), instantiated.error.value_or("unknown WAMR error"));
    instance = std::move(instantiated.instance);
  }

  std::ranges::fill(bytecode, std::byte{0xff});
  const auto started = instance->invoke(mobagen::plugins::WasmPluginExport::Start, {});
  REQUIRE(started.ok());
  CHECK(*started.value == 0U);

  REQUIRE(instance->memory().size() == 64U * 1024U);
  REQUIRE(instance->writable_memory().size() == 64U * 1024U);
  instance->writable_memory()[42] = std::byte{0x5a};
  CHECK(instance->memory()[42] == std::byte{0x5a});

  const auto missing = instance->invoke(mobagen::plugins::WasmPluginExport::Query, {});
  CHECK_FALSE(missing.ok());
  CHECK(missing.error->find("not exported") != std::string::npos);
}

TEST_CASE("WAMR backend: malformed modules and guest traps are structured failures") {
  mobagen::plugins::WamrBackend backend;
  const std::array malformed{std::byte{0x00}, std::byte{0x61}};
  const auto rejected = backend.instantiate(malformed, nullptr);
  CHECK_FALSE(rejected.ok());
  REQUIRE(rejected.error.has_value());
  CHECK_FALSE(rejected.error->empty());

  auto trapping_bytecode = make_module(mobagen::plugins::wasm_plugin_export_name(mobagen::plugins::WasmPluginExport::Start), true);
  auto instantiated = backend.instantiate(trapping_bytecode, nullptr);
  REQUIRE_MESSAGE(instantiated.ok(), instantiated.error.value_or("unknown WAMR error"));
  const auto trapped = instantiated.instance->invoke(mobagen::plugins::WasmPluginExport::Start, {});
  CHECK_FALSE(trapped.ok());
  REQUIRE(trapped.error.has_value());
  CHECK(trapped.error->find("exception") != std::string::npos);
}

TEST_CASE("WAMR backend: an instance rejects calls from a foreign thread") {
  mobagen::plugins::WamrBackend backend;
  auto bytecode = make_module(mobagen::plugins::wasm_plugin_export_name(mobagen::plugins::WasmPluginExport::Start));
  auto instantiated = backend.instantiate(bytecode, nullptr);
  REQUIRE_MESSAGE(instantiated.ok(), instantiated.error.value_or("unknown WAMR error"));

  mobagen::plugins::WasmInvocationResult result;
  std::thread foreign([&] { result = instantiated.instance->invoke(mobagen::plugins::WasmPluginExport::Start, {}); });
  foreign.join();

  CHECK_FALSE(result.ok());
  REQUIRE(result.error.has_value());
  CHECK(result.error->find("owner thread") != std::string::npos);
}

TEST_CASE("WAMR backend: canonical host imports route through the injected instance") {
  using namespace mobagen::plugins;
  constexpr std::string_view message = "WAMR plugin ready";
  constexpr std::string_view capability = "render.backend.v1";
  HostCapture capture;
  auto registry = make_registry();
  auto imports = std::make_shared<WasmHostImports>(
      WasmHostServices{.state = &capture, .log = capture_log, .submit_commands = capture_commands});
  const std::vector<std::string> permissions{"gpu"};
  REQUIRE(imports->bind(registry, permissions));

  WamrBackend backend;
  auto bytecode = make_host_import_module(static_cast<std::uint32_t>(message.size()), static_cast<std::uint32_t>(capability.size()));
  auto instantiated = backend.instantiate(bytecode, imports);
  REQUIRE_MESSAGE(instantiated.ok(), instantiated.error.value_or("unknown WAMR error"));
  auto memory = instantiated.instance->writable_memory();
  REQUIRE(memory.size() == 64U * 1024U);
  write_text(memory, 32, message);
  write_text(memory, 64, capability);
  write_u32(memory, 128, MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE);
  write_u32(memory, 132, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
  write_u32(memory, 136, 160);
  write_u32(memory, 140, MOBAGEN_WASM_COMMAND_HEADER_V1_SIZE);
  write_u32(memory, 144, 1);
  write_u32(memory, 148, 0);
  write_u32(memory, 160, MOBAGEN_WASM_COMMAND_HEADER_V1_SIZE);
  write_u32(memory, 164, 42);

  const auto logged = instantiated.instance->invoke(WasmPluginExport::Start, {});
  REQUIRE(logged.ok());
  CHECK(*logged.value == MOBAGEN_WASM_STATUS_OK);
  CHECK(capture.log_calls == 1);
  CHECK(capture.log_level == 2);
  CHECK(capture.log_message == message);

  const auto found = instantiated.instance->invoke(WasmPluginExport::Query, {});
  REQUIRE(found.ok());
  CHECK(*found.value == MOBAGEN_WASM_STATUS_OK);
  const auto capability_index = registry->find_capability(capability);
  REQUIRE(capability_index.has_value());
  CHECK(read_u32(instantiated.instance->memory(), 96) == capability_index->value);
  CHECK(read_u32(instantiated.instance->memory(), 100) == registry->generation().value);

  const auto submitted = instantiated.instance->invoke(WasmPluginExport::Process, {});
  REQUIRE(submitted.ok());
  CHECK(*submitted.value == MOBAGEN_WASM_STATUS_OK);
  CHECK(capture.submit_calls == 1);
  CHECK(capture.command_count == 1);
  CHECK(capture.permissions == permissions);
  CHECK(read_u32(instantiated.instance->memory(), 208) == MOBAGEN_WASM_STATUS_OK);

  auto without_imports = backend.instantiate(bytecode, nullptr);
  REQUIRE_MESSAGE(without_imports.ok(), without_imports.error.value_or("unknown WAMR error"));
  const auto denied = without_imports.instance->invoke(WasmPluginExport::Start, {});
  REQUIRE(denied.ok());
  CHECK(*denied.value == MOBAGEN_WASM_STATUS_FAILED);
}
