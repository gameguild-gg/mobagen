#include <doctest/doctest.h>

#include "modules/module_manifest.hpp"
#include "plugins/wasm_plugin_loader.hpp"
#include "plugins/wasm_runtime.hpp"
#include "plugins/wamr_backend.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#if !defined(MOBAGEN_TEST_WASI_LUA_GUEST)
#define MOBAGEN_TEST_WASI_LUA_GUEST ""
#endif

namespace {

  using namespace mobagen::plugins;
  namespace fs = std::filesystem;

  std::uint32_t read_u32(const std::byte* memory, std::uint32_t offset) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(memory) + offset;
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U)
           | (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
  }

  void write_u32(std::byte* memory, std::uint32_t offset, std::uint32_t value) {
    memory[offset + 0] = std::byte{static_cast<std::uint8_t>(value & 0xffU)};
    memory[offset + 1] = std::byte{static_cast<std::uint8_t>((value >> 8U) & 0xffU)};
    memory[offset + 2] = std::byte{static_cast<std::uint8_t>((value >> 16U) & 0xffU)};
    memory[offset + 3] = std::byte{static_cast<std::uint8_t>((value >> 24U) & 0xffU)};
  }

  /* Same staging discipline as wasm_quickjs_tests.cpp: allocate through the
     guest's own export, stage source + span pairs, invoke the typed eval
     export through the descriptor-driven dispatcher. */
  struct EvalOutcome {
    bool ok{};
    std::uint32_t status{};
    std::string envelope;
    std::string error;
  };

  /* A trapped runtime error surfaces as a FAILED invoke (the guest traps);
     the host then drains it through mobagen_scripting_error_v1. This
     wrapper hides the two-step from the happy-path callers. */
  EvalOutcome eval_lua(PortableWasmInstance& instance, const std::string& source, bool expect_error_recovery = false) {
    EvalOutcome outcome;
    const std::array allocate_arguments{std::uint32_t{4096}, MOBAGEN_WASM_EXCHANGE_ALIGNMENT};
    auto allocated = instance.invoke(WasmPluginExport::Allocate, allocate_arguments);
    if (!allocated.ok() || *allocated.value == MOBAGEN_WASM_NULL_OFFSET) {
      outcome.error = "guest allocate failed";
      return outcome;
    }
    const std::uint32_t scratch = *allocated.value;
    auto memory = instance.writable_memory();
    std::memcpy(memory.data() + scratch, source.data(), source.size());
    const std::uint32_t source_pair = scratch + 2048;
    const std::uint32_t result_pair = source_pair + 8;
    const std::uint32_t result_offset = scratch + 1024;
    write_u32(memory.data(), source_pair + 0, scratch);
    write_u32(memory.data(), source_pair + 4, static_cast<std::uint32_t>(source.size()));
    write_u32(memory.data(), result_pair + 0, result_offset);
    write_u32(memory.data(), result_pair + 4, 1024);

    mobagen::modules::ModuleExportMarshaling marshaling{};
    marshaling.return_type = mobagen::modules::ModuleMarshalingType::I32;
    marshaling.param_count = 2;
    marshaling.params[0] = mobagen::modules::ModuleMarshalingType::Span;
    marshaling.params[1] = mobagen::modules::ModuleMarshalingType::Span;

    const std::array cells{source_pair, result_pair};
    std::array<std::uint32_t, 2> result_cells{};
    const int status = WamrBackend::wamr_invoke_typed_export(instance, "mobagen_scripting_eval_v1", marshaling, cells, result_cells, &outcome.error);
    outcome.status = result_cells[0];
    const bool trapped = status != 0;
    if (trapped) {
      /* Trap-recovery (todo 17 divergence from QuickJS): the Lua error
         trapped the guest; re-enter through mobagen_scripting_error_v1
         with the SAME result span to finish the unwind + collect the
         message. */
      if (!expect_error_recovery) {
        outcome.error = outcome.error.empty() ? "unexpected guest trap during eval" : outcome.error;
        return outcome;
      }
      mobagen::modules::ModuleExportMarshaling error_marshaling{};
      error_marshaling.return_type = mobagen::modules::ModuleMarshalingType::I32;
      error_marshaling.param_count = 1;
      error_marshaling.params[0] = mobagen::modules::ModuleMarshalingType::Span;
      const std::array error_cells{result_pair};
      std::array<std::uint32_t, 2> error_result_cells{};
      const int drain_status = WamrBackend::wamr_invoke_typed_export(instance, "mobagen_scripting_error_v1", error_marshaling, error_cells,
                                                                     error_result_cells, &outcome.error);
      if (drain_status != 0 || error_result_cells[0] != MOBAGEN_WASM_STATUS_OK) {
        outcome.error = outcome.error.empty() ? "error-drain export failed" : outcome.error;
        return outcome;
      }
      outcome.status = MOBAGEN_WASM_STATUS_OK;
    } else if (result_cells[0] != MOBAGEN_WASM_STATUS_OK) {
      outcome.error = outcome.error.empty() ? "eval export reported status " + std::to_string(result_cells[0]) : outcome.error;
      return outcome;
    }
    const auto view = instance.memory();
    const char* text = reinterpret_cast<const char*>(view.data() + result_offset);
    outcome.envelope.assign(text, strnlen(text, 1024));
    outcome.ok = true;
    return outcome;
  }

}  // namespace

/* Todo 17: the wasi-built Lua guest (Lua 5.4.7, freestanding wasm with
 * trap-based error recovery) evaluates `2*21` through the full loader +
 * WamrBackend path — load → activate → typed eval export → result 42 —
 * and a Lua runtime error surfaces as a module issue inside the result
 * envelope (trap + host-driven drain through mobagen_scripting_error_v1),
 * never a host crash. Absent stage outputs = loud SKIP. */
TEST_CASE("WAMR backend: the wasi-built Lua guest evaluates Lua through the module path") {
  if constexpr (std::string_view{MOBAGEN_TEST_WASI_LUA_GUEST} == "") {
    std::fprintf(stderr, "SKIP: wasi Lua guest path not compiled in\n");
    return;
  } else {
    const fs::path guest_wasm = MOBAGEN_TEST_WASI_LUA_GUEST;
    const fs::path package_dir = guest_wasm.parent_path();
    const fs::path manifest_path = package_dir / portable_wasm_plugin_manifest_filename();
    if (!fs::is_regular_file(guest_wasm) || !fs::is_regular_file(manifest_path)) {
      std::fprintf(stderr, "SKIP: wasi Lua guest absent under %s (run `python3 scripts/toolchains.py fetch wasi-sdk`)\n",
                   package_dir.string().c_str());
      return;
    }
    REQUIRE(fs::file_size(guest_wasm) < 8U * 1024U * 1024U);

    std::ifstream manifest_input(manifest_path, std::ios::binary);
    const std::string manifest_text{std::istreambuf_iterator<char>{manifest_input}, std::istreambuf_iterator<char>{}};
    const auto parsed = mobagen::modules::parse_module_manifest(manifest_text, manifest_path.string());
    REQUIRE(parsed.ok());
    CHECK(parsed.manifest->shared_memory);
    bool eval_export_listed = false;
    for (const auto& entry : parsed.manifest->exports) {
      if (entry.name == "mobagen_scripting_eval_v1") {
        REQUIRE(entry.marshaling.has_value());
        CHECK(entry.marshaling->param_count == 2);
        eval_export_listed = true;
      }
    }
    CHECK(eval_export_listed);

    WamrBackend backend;
    REQUIRE(backend.available());

    auto loaded = load_portable_wasm_plugin_package(package_dir, backend);
    std::string load_error = "unknown load error";
    if (!loaded.issues.empty()) load_error = loaded.issues.front().message;
    REQUIRE_MESSAGE(loaded.ok(), load_error);
    CHECK(loaded.plugin->provider().id == "mobagen.scripting.lua");
    CHECK(std::ranges::find(loaded.plugin->provider().provides, std::string{"mobagen.scripting.v1"})
          != loaded.plugin->provider().provides.end());
    auto instance = loaded.plugin->take_instance();
    REQUIRE(instance != nullptr);

    auto configured = instance->invoke(WasmPluginExport::Configure, std::array<std::uint32_t, 2>{});
    REQUIRE(configured.ok());
    CHECK(*configured.value == MOBAGEN_WASM_STATUS_OK);
    auto started = instance->invoke(WasmPluginExport::Start, {});
    REQUIRE(started.ok());
    CHECK(*started.value == MOBAGEN_WASM_STATUS_OK);

    /* Happy path: return 2*21 -> {"ok":true,"value":"42"}. */
    const auto result = eval_lua(*instance, "return 2*21");
    REQUIRE_MESSAGE(result.ok, result.error);
    CHECK(result.envelope.find("\"ok\":true") != std::string::npos);
    CHECK(result.envelope.find("\"value\":\"42\"") != std::string::npos);

    /* Syntax failure path: no trap, ok:false envelope with the Lua
       message (load errors complete normally through the patched path). */
    const auto syntax = eval_lua(*instance, "then ==", /*expect_error_recovery=*/true);
    REQUIRE_MESSAGE(syntax.ok, syntax.error);
    CHECK(syntax.envelope.find("\"ok\":false") != std::string::npos);

    /* Runtime failure path: error() traps the guest; the host drains
       through mobagen_scripting_error_v1 -> ok:false envelope. */
    const auto runtime_error = eval_lua(*instance, "error('boom')", /*expect_error_recovery=*/true);
    REQUIRE_MESSAGE(runtime_error.ok, runtime_error.error);
    CHECK(runtime_error.envelope.find("\"ok\":false") != std::string::npos);
    CHECK(runtime_error.envelope.find("boom") != std::string::npos);

    /* The guest still evaluates afterwards (the trap recovery restored a
       usable Lua state — the core assertion of the recovery design). */
    const auto after = eval_lua(*instance, "return 6*7");
    REQUIRE_MESSAGE(after.ok, after.error);
    CHECK(after.envelope.find("\"value\":\"42\"") != std::string::npos);

    /* string.format exercises the snprintf chain from the math archive. */
    const auto formatted = eval_lua(*instance, "return string.format('%d-%s', 7, 'x')");
    REQUIRE_MESSAGE(formatted.ok, formatted.error);
    CHECK(formatted.envelope.find("\"value\":\"7-x\"") != std::string::npos);

    /* The pow/log/exp chain exercises the libc.a math members (Lua 5.4
       dropped math.pow; '^' and math.log/exp cover the same libm edges). */
    const auto math_result = eval_lua(*instance, "return math.floor(2^10) + math.floor(math.log(math.exp(1)))");
    REQUIRE_MESSAGE(math_result.ok, math_result.error);
    CHECK(math_result.envelope.find("\"value\":\"1025\"") != std::string::npos);

    auto quiesced = instance->invoke(WasmPluginExport::Quiesce, {});
    REQUIRE(quiesced.ok());
    auto stopped = instance->invoke(WasmPluginExport::Stop, {});
    REQUIRE(stopped.ok());
    instance.reset();
    if (std::getenv("MOBAGEN_LUA_TEST_EARLY")) return;

    /* Activation path on a fresh package load (manager-facing lifecycle). */
    auto second = load_portable_wasm_plugin_package(package_dir, backend);
    std::string second_error = "unknown load error";
    if (!second.issues.empty()) second_error = second.issues.front().message;
    REQUIRE_MESSAGE(second.ok(), second_error);
    auto activated = activate_portable_wasm_plugin(second.plugin->take_instance());
    std::string activation_error = "unknown activation error";
    if (!activated.issues.empty()) activation_error = activated.issues.front().message;
    REQUIRE_MESSAGE(activated.ok(), activation_error);
    REQUIRE(activated.activation->quiesce().ok());
    REQUIRE(activated.activation->stop().ok());
  }
}
