#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

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
