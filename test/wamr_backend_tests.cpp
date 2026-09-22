#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <mobagen/module/module_abi.h>
#include <mobagen/plugin/wasm_abi.h>

#include "modules/capability_registry.hpp"
#include "modules/module_manifest.hpp"
#include "plugins/wamr_backend.hpp"
#include "plugins/wasm_plugin_activation_set.hpp"
#include "plugins/wasm_runtime.hpp"
#include "support/wasm_plugin_test_support.hpp"

namespace {

  /* WAMR AOT magic (config.h 0x746f6100, little-endian "\0aot") + the current
   * AOT package version — synthesized "fake .aot" bytes for the always-run
   * rejection tests (never a real wamrc artifact). */
  constexpr std::array aot_magic_bytes{std::byte{0x00}, std::byte{0x61}, std::byte{0x6f}, std::byte{0x74}};

  std::vector<std::byte> make_fake_aot(std::size_t size) {
    std::vector<std::byte> bytes(size, std::byte{0});
    if (size >= aot_magic_bytes.size()) {
      std::ranges::copy(aot_magic_bytes, bytes.begin());
      if (size >= 8) {
        bytes[4] = std::byte{0x05};  // AOT_CURRENT_VERSION 5 for WAMR 2.4.5
        bytes[5] = std::byte{0x00};
        bytes[6] = std::byte{0x00};
        bytes[7] = std::byte{0x00};
      }
    }
    return bytes;
  }

#if !defined(MOBAGEN_TEST_WAMRC_CACHE_DIR)
#define MOBAGEN_TEST_WAMRC_CACHE_DIR ""
#endif

#if !defined(MOBAGEN_TEST_WAMR_SOURCE_DIR)
#define MOBAGEN_TEST_WAMR_SOURCE_DIR ""
#endif

  /* Runs wamrc (WAMR-2.4.5 prebuilt fetched into external/wamrc-cache/, or any
   * wamrc on PATH built from the same pinned source) over a reference wasm to
   * produce a real .aot fixture at test time. Returns empty when unavailable —
   * callers must SKIP loudly then. */
  std::vector<std::byte> compile_aot_fixture(const std::filesystem::path& wasm_path, const std::filesystem::path& output_path) {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    if (const char* cache = std::getenv("MOBAGEN_WAMRC_PATH"); cache != nullptr && *cache != '\0') candidates.emplace_back(cache);
    if constexpr (std::string_view{MOBAGEN_TEST_WAMRC_CACHE_DIR} != "") {
      candidates.emplace_back(fs::path{MOBAGEN_TEST_WAMRC_CACHE_DIR} / "wamrc" / "wamrc");
      candidates.emplace_back(fs::path{MOBAGEN_TEST_WAMRC_CACHE_DIR} / "wamrc");
    }
    candidates.emplace_back("wamrc");

    fs::path wamrc;
    std::string attempted;
    for (const auto& candidate : candidates) {
      std::error_code ignored;
      if (fs::is_regular_file(candidate, ignored) || candidate == "wamrc") {
        wamrc = candidate;
        break;
      }
      attempted += " " + candidate.string();
    }
    if (wamrc.empty()) {
      std::fprintf(stderr, "SKIP: no wamrc available (tried:%s); set MOBAGEN_WAMRC_PATH or populate external/wamrc-cache/ to enable the real-AOT fixture\n",
                   attempted.c_str());
      return {};
    }

#if defined(__aarch64__) || defined(_M_ARM64)
    constexpr const char* target = "aarch64";
#elif defined(__x86_64__) || defined(_M_X64)
    constexpr const char* target = "x86_64";
#else
    std::fprintf(stderr, "SKIP: real-AOT fixture has no target mapping on this CPU\n");
    return {};
#endif

    const auto command = wamrc.string() + " --target=" + target + " -o " + output_path.string() + " " + wasm_path.string() + " 2>&1";
    const auto status = std::system(command.c_str());
    std::error_code ignored;
    if (status != 0 || !fs::is_regular_file(output_path, ignored) || fs::file_size(output_path, ignored) == 0) {
      std::fprintf(stderr, "SKIP: wamrc failed to produce the .aot fixture (exit %d)\n", status);
      fs::remove(output_path, ignored);
      return {};
    }

    std::ifstream input(output_path, std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    return {reinterpret_cast<const std::byte*>(bytes.data()), reinterpret_cast<const std::byte*>(bytes.data() + bytes.size())};
  }

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

  std::vector<std::byte> make_module(std::string_view function_name, bool traps = false, std::uint32_t memory_pages = 1, bool grows_memory = false,
                                     std::string_view memory_export = MOBAGEN_WASM_MEMORY_EXPORT_V1) {
    std::vector<std::byte> module{
        std::byte{0x00}, std::byte{0x61}, std::byte{0x73}, std::byte{0x6d}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
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

    std::vector<std::byte> memory_section{std::byte{0x01}, std::byte{0x00}};
    append_u32_leb(memory_section, memory_pages);
    append_section(module, 5, memory_section);

    std::vector<std::byte> export_section;
    append_u32_leb(export_section, 2);
    append_name(export_section, memory_export);
    export_section.push_back(std::byte{0x02});
    export_section.push_back(std::byte{0x00});
    append_name(export_section, function_name);
    export_section.push_back(std::byte{0x00});
    export_section.push_back(std::byte{0x00});
    append_section(module, 7, export_section);

    std::vector<std::byte> body{std::byte{0x00}};
    if (traps) {
      body.push_back(std::byte{0x00});
    } else if (grows_memory) {
      append_i32_const(body, 1);
      body.insert(body.end(), {std::byte{0x40}, std::byte{0x00}, std::byte{0x1a}});
      append_i32_const(body, 0);
    } else {
      append_i32_const(body, 0);
    }
    body.push_back(std::byte{0x0b});
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
        std::byte{0x00}, std::byte{0x61}, std::byte{0x73}, std::byte{0x6d}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
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
    append_name(exports, MOBAGEN_WASM_MEMORY_EXPORT_V1);
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

  std::uint32_t capture_commands(void* state, mobagen::plugins::WasmCommandBatchView batch, std::span<const std::string> permissions) noexcept {
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

  std::vector<std::byte> make_reference_plugin_module() {
    std::vector<std::byte> module{
        std::byte{0x00}, std::byte{0x61}, std::byte{0x73}, std::byte{0x6d}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };

    std::vector<std::byte> types;
    append_u32_leb(types, 3);
    for (const std::uint32_t parameter_count : {2U, 3U, 0U}) {
      types.push_back(std::byte{0x60});
      append_u32_leb(types, parameter_count);
      for (std::uint32_t parameter = 0; parameter < parameter_count; ++parameter) types.push_back(std::byte{0x7f});
      types.insert(types.end(), {std::byte{0x01}, std::byte{0x7f}});
    }
    append_section(module, 1, types);

    std::vector<std::byte> imports;
    append_u32_leb(imports, 1);
    append_name(imports, MOBAGEN_WASM_IMPORT_MODULE_V1);
    append_name(imports, MOBAGEN_WASM_IMPORT_SUBMIT_COMMANDS_V1);
    imports.insert(imports.end(), {std::byte{0x00}, std::byte{0x00}});
    append_section(module, 2, imports);

    const std::array function_section{
        std::byte{0x08}, std::byte{0x00}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x02}, std::byte{0x02}, std::byte{0x02}, std::byte{0x01},
    };
    append_section(module, 3, function_section);
    const std::array memory_section{std::byte{0x01}, std::byte{0x00}, std::byte{0x01}};
    append_section(module, 5, memory_section);

    std::vector<std::byte> exports;
    append_u32_leb(exports, 9);
    append_name(exports, MOBAGEN_WASM_MEMORY_EXPORT_V1);
    exports.insert(exports.end(), {std::byte{0x02}, std::byte{0x00}});
    for (std::size_t index = 0; index < 8; ++index) {
      append_name(exports, mobagen::plugins::wasm_plugin_export_name(static_cast<mobagen::plugins::WasmPluginExport>(index)));
      exports.push_back(std::byte{0x00});
      append_u32_leb(exports, static_cast<std::uint32_t>(index + 1));
    }
    append_section(module, 7, exports);

    auto status_body = [](std::int32_t status) {
      std::vector<std::byte> body{std::byte{0x00}};
      append_i32_const(body, status);
      body.push_back(std::byte{0x0b});
      return body;
    };
    std::vector<std::byte> code;
    append_u32_leb(code, 8);
    append_function_body(code, status_body(8));
    append_function_body(code, status_body(MOBAGEN_WASM_STATUS_OK));
    std::vector<std::byte> query_body{std::byte{0x00}, std::byte{0x20}, std::byte{0x00}};
    append_i32_const(query_body, 512);
    append_i32_const(query_body, MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
    query_body.insert(query_body.end(), {std::byte{0xfc}, std::byte{0x0a}, std::byte{0x00}, std::byte{0x00}});
    append_i32_const(query_body, MOBAGEN_WASM_STATUS_OK);
    query_body.push_back(std::byte{0x0b});
    append_function_body(code, query_body);
    append_function_body(code, status_body(MOBAGEN_WASM_STATUS_OK));
    std::vector<std::byte> start_body{std::byte{0x00}};
    append_i32_const(start_body, 224);
    append_i32_const(start_body, 280);
    start_body.insert(start_body.end(), {std::byte{0x10}, std::byte{0x00}, std::byte{0x0b}});
    append_function_body(code, start_body);
    append_function_body(code, status_body(MOBAGEN_WASM_STATUS_OK));
    append_function_body(code, status_body(MOBAGEN_WASM_STATUS_OK));
    append_function_body(code, status_body(MOBAGEN_WASM_STATUS_OK));
    append_section(module, 10, code);

    constexpr std::string_view provider_id = "mobagen.wamr-reference";
    constexpr std::string_view capability = "runtime.wamr.v1";
    constexpr std::string_view permission = "gpu";
    constexpr std::size_t descriptor_template_offset = 512;
    std::vector<std::byte> image(descriptor_template_offset + MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
    write_u32(image, descriptor_template_offset, MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
    write_u32(image, descriptor_template_offset + 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
    write_u32(image, descriptor_template_offset + 8, 96);
    write_u32(image, descriptor_template_offset + 12, static_cast<std::uint32_t>(provider_id.size()));
    write_u32(image, descriptor_template_offset + 16, 1);
    write_u32(image, descriptor_template_offset + 20, 0);
    write_u32(image, descriptor_template_offset + 24, 0);
    write_u32(image, descriptor_template_offset + 28, MOBAGEN_WASM_RELOAD_RESTART);
    write_u32(image, descriptor_template_offset + 32, 160);
    write_u32(image, descriptor_template_offset + 36, 1);
    write_u32(image, descriptor_template_offset + 72, 176);
    write_u32(image, descriptor_template_offset + 76, 1);
    write_text(image, 96, provider_id);
    write_text(image, 128, capability);
    write_text(image, 148, permission);
    write_u32(image, 160, 128);
    write_u32(image, 164, static_cast<std::uint32_t>(capability.size()));
    write_u32(image, 176, 148);
    write_u32(image, 180, static_cast<std::uint32_t>(permission.size()));
    write_u32(image, 224, MOBAGEN_WASM_COMMAND_BATCH_V1_SIZE);
    write_u32(image, 228, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
    write_u32(image, 232, 256);
    write_u32(image, 236, MOBAGEN_WASM_COMMAND_HEADER_V1_SIZE);
    write_u32(image, 240, 1);
    write_u32(image, 244, 0);
    write_u32(image, 256, MOBAGEN_WASM_COMMAND_HEADER_V1_SIZE);
    write_u32(image, 260, 42);

    std::vector<std::byte> data;
    append_u32_leb(data, 1);
    data.push_back(std::byte{0x00});
    append_i32_const(data, 8);
    data.push_back(std::byte{0x0b});
    const auto payload = std::span<const std::byte>{image}.subspan(8);
    append_u32_leb(data, static_cast<std::uint32_t>(payload.size()));
    data.insert(data.end(), payload.begin(), payload.end());
    append_section(module, 11, data);

    return module;
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

TEST_CASE("WAMR backend: resource budgets reject oversized or invalid instances") {
  using namespace mobagen::plugins;
  WamrBackend bounded({.stack_size_bytes = 64U * 1024U, .max_memory_pages = 1});
  REQUIRE(bounded.available());
  auto two_page_module = make_module(wasm_plugin_export_name(WasmPluginExport::Start), false, 2);
  const auto oversized = bounded.instantiate(two_page_module, nullptr);
  CHECK_FALSE(oversized.ok());
  REQUIRE(oversized.error.has_value());
  CHECK(oversized.error->find("memory") != std::string::npos);

  auto growth_module = make_module(wasm_plugin_export_name(WasmPluginExport::Start), false, 1, true);
  auto capped = bounded.instantiate(growth_module, nullptr);
  REQUIRE_MESSAGE(capped.ok(), capped.error.value_or("unknown WAMR error"));
  REQUIRE(capped.instance->memory().size() == MOBAGEN_WASM_LINEAR_MEMORY_PAGE_BYTES);
  REQUIRE(capped.instance->invoke(WasmPluginExport::Start, {}).ok());
  CHECK(capped.instance->memory().size() == MOBAGEN_WASM_LINEAR_MEMORY_PAGE_BYTES);

  auto hidden_memory = make_module(wasm_plugin_export_name(WasmPluginExport::Start), false, 1, false, "private_memory");
  const auto hidden = bounded.instantiate(hidden_memory, nullptr);
  CHECK_FALSE(hidden.ok());
  REQUIRE(hidden.error.has_value());
  CHECK(hidden.error->find("export") != std::string::npos);

  WamrBackend invalid({.stack_size_bytes = 0, .max_memory_pages = 0});
  CHECK_FALSE(invalid.available());
  const auto rejected = invalid.instantiate(make_module(wasm_plugin_export_name(WasmPluginExport::Start)), nullptr);
  CHECK_FALSE(rejected.ok());
  REQUIRE(rejected.error.has_value());
  CHECK(rejected.error->find("options") != std::string::npos);
}

TEST_CASE("WAMR backend: canonical host imports route through the injected instance") {
  using namespace mobagen::plugins;
  constexpr std::string_view message = "WAMR plugin ready";
  constexpr std::string_view capability = "render.backend.v1";
  HostCapture capture;
  auto registry = make_registry();
  auto imports = std::make_shared<WasmHostImports>(WasmHostServices{.state = &capture, .log = capture_log, .submit_commands = capture_commands});
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

TEST_CASE("WAMR backend: a dot-plugin package resolves and activates end to end") {
  using namespace mobagen;
  test::TemporaryWasmDirectory directory;
  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  const auto package = directory.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  const auto bytecode = make_reference_plugin_module();
  test::write_binary(package / plugins::portable_wasm_plugin_binary_filename(), bytecode);

  const modules::ProductDescriptor product{
      .name = "wamr-reference-product",
      .modules = {{.alias = "runtime", .provider = "mobagen.wamr-reference"}},
      .plugins = {"plugins/reference.plugin"},
      .profiles = {{.name = "release", .linkage = modules::LinkageMode::Wasm, .editor = false, .permissions = {"gpu"}}},
  };
  HostCapture capture;
  const plugins::WasmHostServices services{.state = &capture, .submit_commands = capture_commands};
  plugins::WamrBackend backend;
  auto probe = backend.instantiate(bytecode, nullptr);
  REQUIRE_MESSAGE(probe.ok(), probe.error.value_or("unknown WAMR error"));
  CHECK(read_u32(probe.instance->memory(), 512) == MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
  CHECK(read_u32(probe.instance->memory(), 516) == MOBAGEN_WASM_PLUGIN_ABI_VERSION);
  const std::array allocate_arguments{MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE, MOBAGEN_WASM_EXCHANGE_ALIGNMENT};
  const auto allocation = probe.instance->invoke(plugins::WasmPluginExport::Allocate, allocate_arguments);
  REQUIRE(allocation.ok());
  CHECK(*allocation.value == 8);
  const std::array query_arguments{*allocation.value, MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE};
  const auto queried = probe.instance->invoke(plugins::WasmPluginExport::Query, query_arguments);
  REQUIRE(queried.ok());
  CHECK(*queried.value == MOBAGEN_WASM_STATUS_OK);
  CHECK(read_u32(probe.instance->memory(), 8) == MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
  CHECK(read_u32(probe.instance->memory(), 12) == MOBAGEN_WASM_PLUGIN_ABI_VERSION);
  probe.instance.reset();
  auto catalog = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend, {}, services);
  std::string catalog_error = "unknown catalog error";
  if (!catalog.issues.empty()) {
    catalog_error = catalog.issues.front().message;
    if (!catalog.issues.front().load_issues.empty()) {
      catalog_error += ": " + catalog.issues.front().load_issues.front().message;
      if (!catalog.issues.front().load_issues.front().query_issues.empty()) {
        const auto& query_issue = catalog.issues.front().load_issues.front().query_issues.front();
        catalog_error += ": " + query_issue.message;
        if (!query_issue.contract_issues.empty()) {
          catalog_error += ": " + query_issue.contract_issues.front().field + " " + query_issue.contract_issues.front().message;
        }
      }
    }
  }
  REQUIRE_MESSAGE(catalog.ok(), catalog_error);
  REQUIRE(catalog.catalog->plugin_count() == 1);
  const auto* loaded = catalog.catalog->plugin(0);
  REQUIRE(loaded != nullptr);
  CHECK(loaded->path() == std::filesystem::weakly_canonical(package / plugins::portable_wasm_plugin_binary_filename()));
  CHECK(loaded->provider().id == "mobagen.wamr-reference");
  CHECK(loaded->provider().version == modules::SemanticVersion{1, 0, 0});
  CHECK(loaded->provider().provides == std::vector<std::string>{"runtime.wamr.v1"});
  CHECK(loaded->provider().permissions == std::vector<std::string>{"gpu"});

  const modules::ResolverOptions options{
      .target = test::portable_target(),
      .profile = "release",
      .aliases = {{.alias = "runtime", .capability = "runtime.wamr.v1"}},
  };
  auto resolution = modules::resolve_modules(product, catalog.catalog->registry(), options);
  REQUIRE(resolution.ok());
  auto activated = plugins::activate_resolved_portable_wasm_plugins(*catalog.catalog, *resolution.resolution);
  REQUIRE(activated.ok());
  REQUIRE(activated.activation->size() == 1);
  const auto* active = activated.activation->plugin(0);
  REQUIRE(active != nullptr);
  CHECK(active->state() == plugins::PortableWasmPluginState::Active);
  CHECK(capture.submit_calls == 1);
  CHECK(capture.command_count == 1);
  CHECK(capture.permissions == std::vector<std::string>{"gpu"});

  CHECK(activated.activation->stop().ok());
  CHECK(active->state() == plugins::PortableWasmPluginState::Stopped);
}

#if MOBAGEN_WAMR_AOT_BUILD

TEST_CASE("WAMR backend: AOT payload selection prefers .aot and falls back to the interpreter") {
  using namespace mobagen::plugins;
  WamrBackend backend;
  REQUIRE(backend.available());
  CHECK(WamrBackend::runtime_wamr_version() == std::string_view{"2.4.5"});
  const auto wasm = make_module(wasm_plugin_export_name(WasmPluginExport::Start));

  SUBCASE("no .aot payload: interpreter fallback") {
    const auto selection = backend.instantiate_prefer_aot(wasm, {}, "2.4.5", nullptr);
    REQUIRE_MESSAGE(selection.result.ok(), selection.result.error.value_or("unknown WAMR error"));
    CHECK_FALSE(selection.used_aot);
    CHECK_FALSE(selection.issue.has_value());
    const auto started = selection.result.instance->invoke(WasmPluginExport::Start, {});
    REQUIRE(started.ok());
    CHECK(*started.value == 0U);
  }

  SUBCASE("truncated .aot payload: loud AotInvalidBinary, no fallback") {
    const auto truncated = make_fake_aot(3);
    const auto selection = backend.instantiate_prefer_aot(wasm, truncated, "", nullptr);
    CHECK_FALSE(selection.result.ok());
    CHECK_FALSE(selection.used_aot);
    REQUIRE(selection.issue.has_value());
    CHECK(*selection.issue == PortableWasmAotIssueCode::AotInvalidBinary);
    CHECK(selection.result.error->find("AOT") != std::string::npos);
  }

  SUBCASE("corrupted .aot payload with valid magic: loud AotInvalidBinary") {
    auto corrupted = make_fake_aot(256);
    for (std::size_t index = 8; index < corrupted.size(); ++index) corrupted[index] = std::byte{0xc3};
    const auto selection = backend.instantiate_prefer_aot(wasm, corrupted, "", nullptr);
    CHECK_FALSE(selection.result.ok());
    CHECK_FALSE(selection.used_aot);
    REQUIRE(selection.issue.has_value());
    CHECK(*selection.issue == PortableWasmAotIssueCode::AotInvalidBinary);
  }

  SUBCASE("version-mismatched manifest toolchain: loud AotVersionMismatch before instantiation") {
    const auto fake = make_fake_aot(64);
    const auto selection = backend.instantiate_prefer_aot(wasm, fake, "2.4.4", nullptr);
    CHECK_FALSE(selection.result.ok());
    CHECK_FALSE(selection.used_aot);
    REQUIRE(selection.issue.has_value());
    CHECK(*selection.issue == PortableWasmAotIssueCode::AotVersionMismatch);
    CHECK(selection.result.error->find("2.4.4") != std::string::npos);
    CHECK(selection.result.error->find(WamrBackend::runtime_wamr_version()) != std::string::npos);
  }

  SUBCASE("matching manifest toolchain version is accepted") {
    const auto fake = make_fake_aot(64);
    const auto selection = backend.instantiate_prefer_aot(wasm, fake, std::string{WamrBackend::runtime_wamr_version()}, nullptr);
    REQUIRE_FALSE(selection.result.ok());  // fake bytes cannot actually load
    REQUIRE(selection.issue.has_value());
    CHECK(*selection.issue == PortableWasmAotIssueCode::AotInvalidBinary);  // version check passed; payload itself is fake
  }
}

TEST_CASE("WAMR backend: a real wamrc fixture loads and invokes through the AOT path") {
  using namespace mobagen::plugins;
  namespace fs = std::filesystem;

  const mobagen::test::TemporaryWasmDirectory directory;
  const auto wasm_path = directory.path() / "reference.wasm";
  const auto aot_path = directory.path() / "reference.aot";
  mobagen::test::write_binary(wasm_path, make_module(wasm_plugin_export_name(WasmPluginExport::Start)));

  const auto aot = compile_aot_fixture(wasm_path, aot_path);
  if (aot.empty()) return;  // SKIP already reported by compile_aot_fixture

  WamrBackend backend;
  REQUIRE(backend.available());
  const auto selection = backend.instantiate_prefer_aot(make_module(wasm_plugin_export_name(WasmPluginExport::Start)), aot,
                                                        WamrBackend::runtime_wamr_version(), nullptr);
  REQUIRE_MESSAGE(selection.result.ok(), selection.result.error.value_or("unknown WAMR error"));
  CHECK(selection.used_aot);

  const auto started = selection.result.instance->invoke(WasmPluginExport::Start, {});
  REQUIRE(started.ok());
  CHECK(*started.value == 0U);
  REQUIRE(selection.result.instance->memory().size() == 64U * 1024U);
}

#else

TEST_CASE("WAMR backend: AOT payloads are rejected on interpreter-only platforms") {
  using namespace mobagen::plugins;
  WamrBackend backend;
  REQUIRE(backend.available());
  const auto wasm = make_module(wasm_plugin_export_name(WasmPluginExport::Start));
  const auto selection = backend.instantiate_prefer_aot(wasm, make_fake_aot(64), "2.4.5", nullptr);
  CHECK_FALSE(selection.result.ok());
  REQUIRE(selection.issue.has_value());
  CHECK(*selection.issue == PortableWasmAotIssueCode::AotUnsupportedPlatform);

  const auto fallback = backend.instantiate_prefer_aot(wasm, {}, "2.4.5", nullptr);
  REQUIRE_MESSAGE(fallback.result.ok(), fallback.result.error.value_or("unknown WAMR error"));
  CHECK_FALSE(fallback.used_aot);
}

#endif

TEST_CASE("WAMR backend: a shared heap region is exposed, attached, and reused") {
  using namespace mobagen::plugins;
  constexpr std::uint32_t heap_bytes = 8U * 1024U;

  WamrBackend plain;
  REQUIRE(plain.available());
  CHECK(plain.shared_heap_region().data == nullptr);
  CHECK(plain.shared_heap_region().size == 0U);

  WamrBackend shared({.shared_heap_size_bytes = heap_bytes});
  REQUIRE(shared.available());
  const auto region = shared.shared_heap_region();
  REQUIRE(region.data != nullptr);
  CHECK(region.size >= heap_bytes);  // WAMR requires page-aligned sizes; the region may be rounded up
  const auto region_size = region.size;
  CHECK((reinterpret_cast<std::uintptr_t>(region.data) % (64U * 1024U)) == 0U);
  region.data[0] = std::byte{0x7f};
  region.data[region_size - 1] = std::byte{0x5a};

  const auto wasm = make_module(wasm_plugin_export_name(WasmPluginExport::Start));
  {
    auto first = shared.instantiate(wasm, nullptr);
    REQUIRE_MESSAGE(first.ok(), first.error.value_or("unknown WAMR error"));
    CHECK(first.instance->writable_memory().size() == 64U * 1024U);
    auto second = shared.instantiate(wasm, nullptr);
    REQUIRE_MESSAGE(second.ok(), second.error.value_or("unknown WAMR error"));
    CHECK(first.instance->invoke(WasmPluginExport::Start, {}).ok());
    CHECK(second.instance->invoke(WasmPluginExport::Start, {}).ok());
  }
  CHECK(shared.shared_heap_region().data[0] == std::byte{0x7f});
  CHECK(shared.shared_heap_region().data[region_size - 1] == std::byte{0x5a});

  WamrBackend bad_heap({.shared_heap_size_bytes = 8U});
  CHECK_FALSE(bad_heap.available());
}

TEST_CASE("WAMR backend: descriptor-driven marshal invokes a two-i32 export with correct values") {
  using namespace mobagen::plugins;
  using mobagen::modules::ModuleMarshalingType;
  using mobagen::modules::ModuleExportMarshaling;

  /* (add: (i32,i32)->i32 via local.get 0; local.get 1; i32.add) plus the
     required memory export; type section also declares ()->void for a void
     probe. */
  std::vector<std::byte> module{
      std::byte{0x00}, std::byte{0x61}, std::byte{0x73}, std::byte{0x6d}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
  };
  std::vector<std::byte> types;
  append_u32_leb(types, 2);
  for (const std::uint32_t parameter_count : {2U, 0U}) {
    types.push_back(std::byte{0x60});
    append_u32_leb(types, parameter_count);
    for (std::uint32_t parameter = 0; parameter < parameter_count; ++parameter) types.push_back(std::byte{0x7f});
    if (parameter_count != 0U) {
      types.push_back(std::byte{0x01});
      types.push_back(std::byte{0x7f});
    } else {
      types.push_back(std::byte{0x00});
    }
  }
  append_section(module, 1, types);

  const std::array function_section{std::byte{0x02}, std::byte{0x00}, std::byte{0x01}};
  append_section(module, 3, function_section);
  const std::array memory_section{std::byte{0x01}, std::byte{0x00}, std::byte{0x01}};
  append_section(module, 5, memory_section);

  std::vector<std::byte> exports;
  append_u32_leb(exports, 2);
  append_name(exports, MOBAGEN_WASM_MEMORY_EXPORT_V1);
  exports.insert(exports.end(), {std::byte{0x02}, std::byte{0x00}});
  append_name(exports, "mobagen_add");
  exports.push_back(std::byte{0x00});
  append_u32_leb(exports, 0);
  append_section(module, 7, exports);

  std::vector<std::byte> code;
  append_u32_leb(code, 2);
  std::vector<std::byte> add_body{std::byte{0x00}, std::byte{0x20}, std::byte{0x00}, std::byte{0x20}, std::byte{0x01}, std::byte{0x6a},
                                  std::byte{0x0b}};
  append_function_body(code, add_body);
  std::vector<std::byte> void_body{std::byte{0x00}, std::byte{0x0b}};
  append_function_body(code, void_body);
  append_section(module, 10, code);

  WamrBackend backend;
  REQUIRE(backend.available());
  auto instantiated = backend.instantiate(module, nullptr);
  REQUIRE_MESSAGE(instantiated.ok(), instantiated.error.value_or("unknown WAMR error"));

  const ModuleExportMarshaling add_marshal{ModuleMarshalingType::I32, 2, {ModuleMarshalingType::I32, ModuleMarshalingType::I32}};
  std::array<std::uint32_t, 2> result_cells{};
  const std::array arguments{std::uint32_t{20}, std::uint32_t{22}};
  const auto status = WamrBackend::wamr_invoke_typed_export(*instantiated.instance, "mobagen_add", add_marshal, arguments, result_cells);
  REQUIRE(status == 0);
  CHECK(result_cells[0] == 42);

  /* Wrong cell count for the descriptor = loud rejection before the call. */
  const std::array one_argument{std::uint32_t{20}};
  CHECK(WamrBackend::wamr_invoke_typed_export(*instantiated.instance, "mobagen_add", add_marshal, one_argument, result_cells) == -3);

  /* Unknown export name = missing-export rejection. */
  CHECK(WamrBackend::wamr_invoke_typed_export(*instantiated.instance, "mobagen_missing", add_marshal, arguments, result_cells) == -1);

  /* ptr cell beyond guest memory = bounds rejection before the call. */
  const ModuleExportMarshaling ptr_marshal{ModuleMarshalingType::I32, 1, {ModuleMarshalingType::Ptr}};
  const std::array out_of_bounds{std::uint32_t{0x7fffffff}};
  CHECK(WamrBackend::wamr_invoke_typed_export(*instantiated.instance, "mobagen_add", ptr_marshal, out_of_bounds, result_cells) == -3);

  /* Same descriptor decodes from a manifest round-trip: the descriptor the
     dispatcher walks is exactly the manifest's derived data. */
  const auto signature = MOBAGEN_MODULE_SIG_2(MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32);
  mobagen::modules::ModuleManifest manifest;
  manifest.api_version = 1;
  manifest.abi_version = 1;
  manifest.entry = "mobagen_module_entry_v1";
  manifest.exports = {{"mobagen_add", signature, {}}};
  const auto parsed = mobagen::modules::parse_module_manifest(mobagen::modules::serialize_module_manifest(manifest));
  REQUIRE(parsed.ok());
  REQUIRE(parsed.manifest->exports[0].marshaling.has_value());
  CHECK(parsed.manifest->exports[0].marshaling->return_type == add_marshal.return_type);
  CHECK(parsed.manifest->exports[0].marshaling->param_count == add_marshal.param_count);
  CHECK(parsed.manifest->exports[0].marshaling->params == add_marshal.params);
  CHECK(WamrBackend::wamr_invoke_typed_export(*instantiated.instance, "mobagen_add", *parsed.manifest->exports[0].marshaling, arguments,
                                              result_cells)
        == 0);
  CHECK(result_cells[0] == 42);
}

TEST_CASE("WAMR backend: a dot-plugin package with a plugin.aot payload resolves through the loader") {
  using namespace mobagen;
  namespace fs = std::filesystem;

  const test::TemporaryWasmDirectory directory;
  HostCapture capture;
  const plugins::WasmHostServices services{.state = &capture, .submit_commands = capture_commands};

  const auto write_package = [&](const std::string& name) {
    const auto package = directory.path() / name;
    REQUIRE(fs::create_directory(package));
    test::write_binary(package / plugins::portable_wasm_plugin_binary_filename(),
                       make_module(plugins::wasm_plugin_export_name(plugins::WasmPluginExport::Start)));
    return package;
  };

  plugins::WamrBackend backend;
  REQUIRE(backend.available());

#if MOBAGEN_WAMR_AOT_BUILD
  const auto rejected = write_package("corrupt.plugin");
  test::write_binary(rejected / plugins::portable_wasm_plugin_aot_filename(), make_fake_aot(3));
  test::write_text(rejected / plugins::portable_wasm_plugin_manifest_filename(),
                   "schema: 2\napi: 1\nabi: 1\nentry: mobagen_module_entry_v1\nexports: []\ntoolchain:\n  producer: wamrc\n  version: "
                       + std::string{plugins::WamrBackend::runtime_wamr_version()} + "\n");
  const auto corrupt = plugins::load_portable_wasm_plugin_package(rejected, backend, services);
  CHECK_FALSE(corrupt.ok());
  REQUIRE_FALSE(corrupt.issues.empty());
  CHECK(corrupt.issues.front().code == plugins::PortableWasmPluginLoadIssueCode::AotRejected);
  REQUIRE(corrupt.issues.front().aot_issue.has_value());
  CHECK(*corrupt.issues.front().aot_issue == plugins::PortableWasmAotIssueCode::AotInvalidBinary);
  CHECK(corrupt.issues.front().message.find("aot-invalid-binary") != std::string::npos);

  const auto mismatched = write_package("mismatch.plugin");
  test::write_binary(mismatched / plugins::portable_wasm_plugin_aot_filename(), make_fake_aot(64));
  test::write_text(mismatched / plugins::portable_wasm_plugin_manifest_filename(),
                   "schema: 2\napi: 1\nabi: 1\nentry: mobagen_module_entry_v1\nexports: []\ntoolchain:\n  producer: wamrc\n  version: 2.4.4\n");
  const auto mismatch = plugins::load_portable_wasm_plugin_package(mismatched, backend, services);
  CHECK_FALSE(mismatch.ok());
  REQUIRE_FALSE(mismatch.issues.empty());
  CHECK(mismatch.issues.front().code == plugins::PortableWasmPluginLoadIssueCode::AotRejected);
  REQUIRE(mismatch.issues.front().aot_issue.has_value());
  CHECK(*mismatch.issues.front().aot_issue == plugins::PortableWasmAotIssueCode::AotVersionMismatch);
  CHECK(mismatch.issues.front().message.find("aot-version-mismatch") != std::string::npos);

  const auto real_aot_package = directory.path() / "real.plugin";
  REQUIRE(fs::create_directory(real_aot_package));
  const auto reference = make_reference_plugin_module();
  test::write_binary(real_aot_package / plugins::portable_wasm_plugin_binary_filename(), reference);
  test::write_binary(directory.path() / "reference.wasm", reference);
  const auto aot = compile_aot_fixture(directory.path() / "reference.wasm", directory.path() / "reference.aot");
  if (!aot.empty()) {
    CHECK(aot.size() > aot_magic_bytes.size());
    test::write_binary(real_aot_package / plugins::portable_wasm_plugin_aot_filename(), aot);
    test::write_text(real_aot_package / plugins::portable_wasm_plugin_manifest_filename(),
                     "schema: 2\napi: 1\nabi: 1\nentry: mobagen_module_entry_v1\nexports: []\ntoolchain:\n  producer: wamrc\n  version: "
                         + std::string{plugins::WamrBackend::runtime_wamr_version()} + "\n");
    const auto loaded = plugins::load_portable_wasm_plugin_package(real_aot_package, backend, services);
    std::string error = "unknown load error";
    if (!loaded.issues.empty()) error = loaded.issues.front().message;
    REQUIRE_MESSAGE(loaded.ok(), error);
    CHECK(loaded.plugin->provider().id == "mobagen.wamr-reference");
  }
#endif

  const auto fallback = directory.path() / "fallback.plugin";
  REQUIRE(fs::create_directory(fallback));
  test::write_binary(fallback / plugins::portable_wasm_plugin_binary_filename(), make_reference_plugin_module());
  const auto plain = plugins::load_portable_wasm_plugin_package(fallback, backend, services);
  std::string plain_error = "unknown load error";
  if (!plain.issues.empty()) plain_error = plain.issues.front().message;
  CHECK_MESSAGE(plain.ok(), plain_error);
  CHECK(plain.plugin.has_value());
}
