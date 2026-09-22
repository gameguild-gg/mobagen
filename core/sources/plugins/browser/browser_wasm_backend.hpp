#pragma once

#include "../wasm_plugin_loader.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mobagen::plugins {

  /*
   * Chromium main-thread synchronous WebAssembly.Module compile budget
   * (M115+). Above it the sync constructor THROWS (RangeError) instead of
   * degrading; modules over the budget are rejected up front with a clear
   * diagnostic. Async/worker pre-compile is explicitly out of scope here.
   */
  inline constexpr std::size_t browser_wasm_sync_compile_budget_bytes = 8U * 1024U * 1024U;

  /*
   * Browser backend for portable WASM plugins (emscripten targets only; the
   * translation unit is compiled solely under EMSCRIPTEN, both web variants).
   *
   * instantiate() consumes the already-read plugin bytes with the SYNCHRONOUS
   * WebAssembly.Module + WebAssembly.Instance constructors — the loader reads
   * bytes before calling instantiate, so no interface change is needed and the
   * "must synchronously consume" contract is preserved. The guest module owns
   * its own linear memory (STANDALONE_WASM with --export-memory), separate
   * from the engine's wasmMemory.
   *
   * MEMORY MIRROR: a wasm-compiled host cannot form native pointers into the
   * guest's separate WebAssembly.Memory buffer, so memory()/writable_memory()
   * expose an engine-owned mirror instead: refreshed from guest memory before
   * every view, flushed back to the guest before every invoke. This honors the
   * PortableWasmInstance contract ("views remain valid only until the next
   * invoke call"). Guests built per the plugin ABI keep a fixed-size linear
   * memory, so a growing guest buffer is an error, not a silent truncation.
   *
   * HOST IMPORTS: guest imports under the "mobagen_v1" module dispatch through
   * browser_wasm_dispatch_host_import — the seam todo 10's descriptor-driven
   * marshaller retro-wires. This backend ships trivial direct dispatch into
   * the instance's retained WasmHostImports, mirroring the WAMR backend's
   * import shims. No eval / new Function anywhere (CSP rule).
   */
  class BrowserWasmBackend final : public PortableWasmBackend {
  public:
    BrowserWasmBackend() = default;
    ~BrowserWasmBackend() override;

    [[nodiscard]] PortableWasmInstantiationResult instantiate(std::span<const std::byte> binary,
                                                              std::shared_ptr<WasmHostImports> host_imports) override;
  };

#if defined(__EMSCRIPTEN__)
  /*
   * Test/inspection seam (also usable by the todo 10 marshaller): invokes an
   * arbitrary named u32(u32,u32) export on a BrowserWasmBackend instance.
   * Returns 0 and stores the u32 result; -1 when the export is missing or the
   * instance belongs to another backend; -2 on other failure.
   */
  [[nodiscard]] int browser_wasm_call_named_export(PortableWasmInstance& instance, std::string_view export_name, std::uint32_t a,
                                                   std::uint32_t b, std::uint32_t* out_result) noexcept;
#endif

}  // namespace mobagen::plugins
