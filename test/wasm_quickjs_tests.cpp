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
#include <string>
#include <string_view>
#include <vector>

#if !defined(MOBAGEN_TEST_WASI_QUICKJS_GUEST)
#define MOBAGEN_TEST_WASI_QUICKJS_GUEST ""
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

  /* Stages `source` + the span {offset,size} pairs into the guest through
     its own allocate export, then invokes mobagen_scripting_eval_v1 through
     the descriptor-driven typed dispatcher (todo 10). */
  struct EvalOutcome {
    bool ok{};
    std::uint32_t status{};
    std::string envelope;
    std::string error;
  };

  EvalOutcome eval_js(PortableWasmInstance& instance, const std::string& source) {
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
    if (status != 0 || result_cells[0] != MOBAGEN_WASM_STATUS_OK) {
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

/* Todo 16: the wasi-built QuickJS guest (quickjs-ng v0.10.0, freestanding
 * wasm) evaluates `1+1` through the full loader + WamrBackend path —
 * load (manifest-validated package) → activate → typed eval export → result
 * 2 — and a JS syntax error surfaces as a module issue inside the result
 * envelope, never a host crash. Absent stage outputs = loud SKIP. */
TEST_CASE("WAMR backend: the wasi-built QuickJS guest evaluates JavaScript through the module path") {
  if constexpr (std::string_view{MOBAGEN_TEST_WASI_QUICKJS_GUEST} == "") {
    std::fprintf(stderr, "SKIP: wasi QuickJS guest path not compiled in\n");
    return;
  } else {
    const fs::path guest_wasm = MOBAGEN_TEST_WASI_QUICKJS_GUEST;
    const fs::path package_dir = guest_wasm.parent_path();
    const fs::path manifest_path = package_dir / portable_wasm_plugin_manifest_filename();
    if (!fs::is_regular_file(guest_wasm) || !fs::is_regular_file(manifest_path)) {
      std::fprintf(stderr, "SKIP: wasi QuickJS guest absent under %s (run `python3 scripts/toolchains.py fetch wasi-sdk`)\n",
                   package_dir.string().c_str());
      return;
    }
    REQUIRE(fs::file_size(guest_wasm) < 8U * 1024U * 1024U);

    /* Manifest carries the shared-memory capability stamp (todo 18) and the
       scripting export descriptors (span params, todo 10). */
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

    /* Full loader path: package → manifest validation → instantiate →
       descriptor query. The instance is kept here for direct typed-export
       invokes; the loader lifecycle (activate/quiesce/stop) is exercised
       through activate_portable_wasm_plugin on a SECOND package load so
       the scripting calls and the lifecycle stay on fresh instances. */
    auto loaded = load_portable_wasm_plugin_package(package_dir, backend);
    std::string load_error = "unknown load error";
    if (!loaded.issues.empty()) load_error = loaded.issues.front().message;
    REQUIRE_MESSAGE(loaded.ok(), load_error);
    CHECK(loaded.plugin->provider().id == "mobagen.scripting.quickjs");
    CHECK(std::ranges::find(loaded.plugin->provider().provides, std::string{"mobagen.scripting.v1"})
          != loaded.plugin->provider().provides.end());
    auto instance = loaded.plugin->take_instance();
    REQUIRE(instance != nullptr);

    /* Configure + start through the coarse lifecycle exports (the same
       calls the activation path drives), then eval. */
    auto configured = instance->invoke(WasmPluginExport::Configure, std::array<std::uint32_t, 2>{});
    REQUIRE(configured.ok());
    CHECK(*configured.value == MOBAGEN_WASM_STATUS_OK);
    auto started = instance->invoke(WasmPluginExport::Start, {});
    REQUIRE(started.ok());
    CHECK(*started.value == MOBAGEN_WASM_STATUS_OK);

    /* Happy path: 1+1 -> {"ok":true,"value":"2"}. */
    const auto result = eval_js(*instance, "1+1");
    REQUIRE_MESSAGE(result.ok, result.error);
    CHECK(result.envelope.find("\"ok\":true") != std::string::npos);
    CHECK(result.envelope.find("\"value\":\"2\"") != std::string::npos);

    /* Failure path: a JS syntax error is a module issue in the envelope —
       the guest keeps running and the host never crashes. */
    const auto broken = eval_js(*instance, "function {{{");
    REQUIRE_MESSAGE(broken.ok, broken.error);
    CHECK(broken.envelope.find("\"ok\":false") != std::string::npos);
    CHECK(broken.envelope.find("SyntaxError") != std::string::npos);

    /* The guest still evaluates afterwards (sandbox survived the error). */
    const auto after = eval_js(*instance, "6*7");
    REQUIRE_MESSAGE(after.ok, after.error);
    CHECK(after.envelope.find("\"value\":\"42\"") != std::string::npos);

    auto quiesced = instance->invoke(WasmPluginExport::Quiesce, {});
    REQUIRE(quiesced.ok());
    auto stopped = instance->invoke(WasmPluginExport::Stop, {});
    REQUIRE(stopped.ok());

    /* Activation path (load → activate → quiesce → stop) on a fresh package
       load: proves the full manager-facing lifecycle works for the guest. */
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
