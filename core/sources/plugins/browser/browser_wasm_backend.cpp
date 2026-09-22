#include "browser_wasm_backend.hpp"

/*
 * Emscripten-only translation unit (both web variants from todo 5; CMake
 * compiles this directory only under EMSCRIPTEN). If you are reading this on
 * a native build it is not compiled.
 *
 * Exceptions are DISABLED in this project's web builds, so nothing here may
 * throw past instantiate() — every failure path returns
 * PortableWasmInstantiationResult::failure, which wasm_plugin_loader maps to
 * the existing BackendFailure load issue.
 */
#include <emscripten.h>

#include <mobagen/plugin/wasm_abi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/* JS runtime symbols referenced from the EM_JS bodies below. */
EM_JS_DEPS(mobagen_browser_wasm, "$UTF8ToString,$stringToUTF8");

namespace mobagen::plugins {
  namespace {

    constexpr std::size_t export_count = static_cast<std::size_t>(WasmPluginExport::Process) + 1U;
    constexpr std::size_t max_argument_cells = 8U;

    class BrowserWasmInstance;

    /*
     * JS-side instance pool. globalThis.__mobagenBrowserWasm[handle] holds
     * {instance} for handle h; the C++ side owns the integer handle plus the
     * memory mirror. Guests become garbage-collectable once the C++ side
     * drops the record (dtor) — the engine heap never keeps them alive.
     *
     * The plugin bytes are handed to the SYNCHRONOUS WebAssembly.Module
     * constructor as a view over the engine heap (HEAPU8.subarray) — the
     * constructor copies synchronously, which satisfies the backend contract
     * "must synchronously consume" without interface changes. Above
     * Chromium's ~8 MiB main-thread budget this constructor THROWS
     * (RangeError); instantiate() pre-rejects those sizes and the JS catch
     * still maps any residual throw into a failure string.
     */
    EM_JS(int, mobagen_browser_wasm_instantiate_js,
          (unsigned int ptr, unsigned int len, char* error_out, int error_cap), {
            try {
              var pool = (globalThis.__mobagenBrowserWasm = globalThis.__mobagenBrowserWasm || []);
              var module = new WebAssembly.Module(HEAPU8.subarray(ptr, ptr + len));
              var instance = new WebAssembly.Instance(module, {
                mobagen_v1: {
                  log: function(level, messageOffset, messageSize) {
                    return _mobagen_browser_wasm_dispatch_c(0, level, messageOffset, messageSize, 0);
                  },
                  find_capability: function(capabilityOffset, capabilitySize, capabilityVersion, outputHandleOffset) {
                    return _mobagen_browser_wasm_dispatch_c(1, capabilityOffset, capabilitySize, capabilityVersion, outputHandleOffset);
                  },
                  submit_commands: function(inputBatchOffset, resultOffset) {
                    return _mobagen_browser_wasm_dispatch_c(2, inputBatchOffset, resultOffset, 0, 0);
                  }
                }
              });
              pool.push({instance: instance});
              return pool.length - 1;
            } catch (e) {
              try {
                var message = "browser WASM instantiation failed: " + ((e && e.message) ? e.message : String(e));
                if (error_out != 0 && error_cap > 0) stringToUTF8(message, error_out, error_cap);
              } catch (ignored) {
              }
              return -1;
            }
          });

    EM_JS(void, mobagen_browser_wasm_drop_js, (int handle), {
      var pool = globalThis.__mobagenBrowserWasm;
      if (pool) pool[handle] = undefined;
    });

    /* Guest linear memory size in bytes; -2 when 'memory' is not an exported
       WebAssembly.Memory, -1 for an unknown handle. */
    EM_JS(int, mobagen_browser_wasm_memory_size_js, (int handle), {
      var record = globalThis.__mobagenBrowserWasm[handle];
      if (record === undefined || record === null) return -1;
      var memory = record.instance.exports.memory;
      if (!(memory instanceof WebAssembly.Memory)) return -2;
      return memory.buffer.byteLength;
    });

    /* Copy guest memory into the engine-heap mirror [ptr, ptr+cap). Returns
       the byte length copied; -1 when the guest memory outgrew the mirror
       (fixed-size ABI guests never do). */
    EM_JS(int, mobagen_browser_wasm_refresh_js, (int handle, unsigned int ptr, unsigned int cap), {
      var memory = globalThis.__mobagenBrowserWasm[handle].instance.exports.memory;
      if (memory.buffer.byteLength > cap) return -1;
      HEAPU8.set(new Uint8Array(memory.buffer), ptr);
      return memory.buffer.byteLength;
    });

    /* Copy the mirror [ptr, ptr+len) back into guest memory. */
    EM_JS(int, mobagen_browser_wasm_flush_js, (int handle, unsigned int ptr, unsigned int len), {
      var memory = globalThis.__mobagenBrowserWasm[handle].instance.exports.memory;
      new Uint8Array(memory.buffer).set(HEAPU8.subarray(ptr, ptr + len));
      return 0;
    });

    EM_JS(int, mobagen_browser_wasm_has_export_js, (int handle, const char* name_ptr), {
      var exported = globalThis.__mobagenBrowserWasm[handle].instance.exports[UTF8ToString(name_ptr)];
      return typeof exported === "function" ? 1 : 0;
    });

    /* Invoke an i32 export with argc u32 arguments read from argv. Sets
       *missing_out (engine-heap int) when the export is absent. i64-returning
       exports are not part of the plugin ABI and are rejected by the guest
       contract, not handled here. */
    EM_JS(int, mobagen_browser_wasm_call_export_js, (int handle, const char* name_ptr, unsigned int argv, int argc, int* missing_out), {
      var fn = globalThis.__mobagenBrowserWasm[handle].instance.exports[UTF8ToString(name_ptr)];
      if (typeof fn !== "function") {
        HEAPU32[missing_out >> 2] = 1;
        return 0;
      }
      HEAPU32[missing_out >> 2] = 0;
      var args = [];
      for (var i = 0; i < argc; ++i) args.push(HEAPU32[(argv >> 2) + i]);
      return fn.apply(null, args) | 0;
    });

    /* The instance whose guest is currently executing (set around every
       invoke). Guest imports can only run inside an invoke frame, so this is
       always bound when the dispatch shim fires. JS is single-threaded. */
    BrowserWasmInstance* g_dispatch_instance = nullptr;

    std::vector<BrowserWasmInstance*>& instance_registry() {
      static std::vector<BrowserWasmInstance*> registry;
      return registry;
    }

    unsigned int heap_pointer(const void* pointer) noexcept {
      return static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(pointer));
    }

    /*
     * PortableWasmInstance over a browser-side WebAssembly.Instance.
     *
     * MEMORY MIRROR: the host engine itself compiles to wasm, so it cannot
     * form native pointers into the guest's separate WebAssembly.Memory
     * buffer. memory()/writable_memory() therefore expose an engine-heap
     * mirror: guest -> mirror before every view, mirror -> guest before every
     * invoke (and after host-import writes). The PortableWasmInstance
     * contract — "memory views remain valid only until the next invoke call"
     * — is satisfied: held spans stay valid until the next view/invoke, and
     * exchange-buffer flows (allocate -> write -> configure) complete within
     * one view window. Guests per the plugin ABI keep fixed-size linear
     * memory; a guest that outgrew the mirror invalidates views loudly
     * (empty span) instead of truncating.
     */
    class BrowserWasmInstance final : public PortableWasmInstance {
    public:
      BrowserWasmInstance(int handle, std::size_t guest_memory_bytes, std::shared_ptr<WasmHostImports> host_imports)
          : PortableWasmInstance(std::move(host_imports)), handle_(handle) {
        mirror_.resize(guest_memory_bytes);
        auto& registry = instance_registry();
        if (static_cast<std::size_t>(handle_) >= registry.size()) registry.resize(static_cast<std::size_t>(handle_) + 1U, nullptr);
        registry[static_cast<std::size_t>(handle_)] = this;
        for (std::size_t index = 0; index < present_.size(); ++index) {
          const auto name = wasm_plugin_export_name(static_cast<WasmPluginExport>(index));
          present_[index] = mobagen_browser_wasm_has_export_js(handle_, name.data()) != 0;
        }
      }

      ~BrowserWasmInstance() override {
        instance_registry()[static_cast<std::size_t>(handle_)] = nullptr;
        mobagen_browser_wasm_drop_js(handle_);
      }

      [[nodiscard]] int handle() const noexcept { return handle_; }

      [[nodiscard]] WasmInvocationResult invoke(WasmPluginExport function, std::span<const std::uint32_t> arguments) override {
        if (arguments.size() > max_argument_cells) return WasmInvocationResult::failure("browser WASM invocation has too many argument cells");
        const auto index = static_cast<std::size_t>(function);
        if (index >= present_.size()) return WasmInvocationResult::failure("unknown Mobagen WASM export");
        if (!present_[index])
          return WasmInvocationResult::failure(std::string{wasm_plugin_export_name(function)} + " is not exported by WASM plugin");

        /* Flushing a mirror with no unflushed host writes would overwrite
           authoritative guest state with a stale snapshot — dirty tracking
           guards that. */
        if (mirror_dirty_ && !flush_mirror()) return WasmInvocationResult::failure("browser WASM guest memory flush failed before invoke");

        std::array<std::uint32_t, max_argument_cells> cells{};
        std::ranges::copy(arguments, cells.begin());
        int missing = 0;
        const auto dispatched = g_dispatch_instance;
        g_dispatch_instance = this;
        /* A guest trap aborts the engine runtime (web builds ship without
           exception handling); coarse ABI callbacks are not expected to trap. */
        const int result = mobagen_browser_wasm_call_export_js(handle_, wasm_plugin_export_name(function).data(), heap_pointer(cells.data()),
                                                               static_cast<int>(arguments.size()), &missing);
        g_dispatch_instance = dispatched;
        if (missing != 0)
          return WasmInvocationResult::failure(std::string{wasm_plugin_export_name(function)} + " is not exported by WASM plugin");
        return WasmInvocationResult::success(static_cast<std::uint32_t>(result));
      }

      [[nodiscard]] std::span<const std::byte> memory() const noexcept override {
        const auto view = refresh();
        return {view.data(), view.size()};
      }

      [[nodiscard]] std::span<std::byte> writable_memory() noexcept {
        mirror_dirty_ = true;
        return refresh();
      }

      /* Host-import dispatch: services write through the mirror view, so copy
         the mirror back to the guest before returning into guest code. */
      void flush_mirror_for_dispatch() noexcept { (void)flush_mirror(); }

    private:
      [[nodiscard]] std::span<std::byte> refresh() const noexcept {
        if (mirror_.empty() || !mirror_valid_) return {};
        if (mobagen_browser_wasm_refresh_js(handle_, heap_pointer(mirror_.data()), static_cast<unsigned int>(mirror_.size())) < 0) {
          mirror_valid_ = false;
          return {};
        }
        return mirror_;
      }

      [[nodiscard]] bool flush_mirror() const noexcept {
        if (mirror_.empty()) return true;
        if (!mirror_valid_) return false;
        if (mobagen_browser_wasm_flush_js(handle_, heap_pointer(mirror_.data()), static_cast<unsigned int>(mirror_.size())) != 0) {
          mirror_valid_ = false;
          return false;
        }
        mirror_dirty_ = false;
        return true;
      }

      int handle_;
      mutable std::vector<std::byte> mirror_;
      mutable bool mirror_valid_{true};
      mutable bool mirror_dirty_{false};
      std::array<bool, export_count> present_{};
    };

    /* Type proof without RTTI: registry membership implies the dynamic type. */
    BrowserWasmInstance* registered_instance(const PortableWasmInstance* instance) noexcept {
      for (auto* candidate : instance_registry()) {
        if (candidate == instance) return candidate;
      }
      return nullptr;
    }

  }  // namespace

  /*
   * Host-import dispatch seam (todo 10 retro-wires descriptor-driven
   * marshalling here). Trivial direct dispatch today: view the mirror so the
   * service reads the guest's current bytes, forward into the retained
   * WasmHostImports exactly like the WAMR backend's shims, then flush the
   * mirror back so writes (find_capability handle, submit_commands result)
   * become visible to the running guest.
   */
  extern "C" EMSCRIPTEN_KEEPALIVE int mobagen_browser_wasm_dispatch_c(int function, unsigned int a, unsigned int b, unsigned int c,
                                                                      unsigned int d) {
    auto* const instance = g_dispatch_instance;
    if (instance == nullptr) return MOBAGEN_WASM_STATUS_FAILED;
    const auto* const imports = instance->host_imports();
    if (imports == nullptr) return MOBAGEN_WASM_STATUS_FAILED;

    const auto memory = instance->writable_memory();
    std::uint32_t status = MOBAGEN_WASM_STATUS_FAILED;
    switch (function) {
      case 0:
        status = imports->log(memory, a, b, c);
        break;
      case 1:
        status = imports->find_capability(memory, a, b, c, d);
        break;
      case 2:
        status = imports->submit_commands(memory, a, b);
        break;
      default:
        return MOBAGEN_WASM_STATUS_FAILED;
    }
    instance->flush_mirror_for_dispatch();
    return status;
  }

  BrowserWasmBackend::~BrowserWasmBackend() = default;

  PortableWasmInstantiationResult BrowserWasmBackend::instantiate(std::span<const std::byte> binary,
                                                                  std::shared_ptr<WasmHostImports> host_imports) {
    if (binary.empty()) return PortableWasmInstantiationResult::failure("browser WASM cannot instantiate an empty module");
    if (binary.size() > browser_wasm_sync_compile_budget_bytes) {
      const auto mib = binary.size() / (1024U * 1024U);
      std::fprintf(stderr,
                   "[browser-wasm] WARNING: module is %zu MiB, above the 8 MiB Chromium main-thread synchronous-compile budget; "
                   "the sync WebAssembly.Module constructor would throw RangeError\n",
                   mib);
      return PortableWasmInstantiationResult::failure(
          "browser WASM module is " + std::to_string(mib)
          + " MiB, above the 8 MiB Chromium main-thread synchronous-compile budget (the sync WebAssembly.Module constructor "
            "throws RangeError there); compile it in a worker or split the module");
    }

    char error[512]{};
    const int handle = mobagen_browser_wasm_instantiate_js(heap_pointer(binary.data()), static_cast<unsigned int>(binary.size()), error,
                                                           static_cast<int>(sizeof error));
    if (handle < 0) {
      return PortableWasmInstantiationResult::failure(error[0] != '\0' ? std::string{error}
                                                                       : std::string{"browser WASM instantiation failed"});
    }

    const auto guest_memory_bytes = mobagen_browser_wasm_memory_size_js(handle);
    if (guest_memory_bytes < 0) {
      mobagen_browser_wasm_drop_js(handle);
      return PortableWasmInstantiationResult::failure(guest_memory_bytes == -2
                                                          ? std::string{"browser WASM module must export its linear memory as 'memory'"}
                                                          : std::string{"browser WASM instance handle is invalid"});
    }

    try {
      auto instance = std::make_unique<BrowserWasmInstance>(handle, static_cast<std::size_t>(guest_memory_bytes), std::move(host_imports));
      return PortableWasmInstantiationResult::success(std::move(instance));
    } catch (const std::bad_alloc&) {
      mobagen_browser_wasm_drop_js(handle);
      return PortableWasmInstantiationResult::failure("browser WASM instance allocation failed");
    }
  }

  int browser_wasm_call_named_export(PortableWasmInstance& instance, std::string_view export_name, std::uint32_t a, std::uint32_t b,
                                     std::uint32_t* out_result) noexcept {
    auto* const self = registered_instance(&instance);
    if (self == nullptr || out_result == nullptr) return -1;

    char name[128]{};
    const auto length = std::min(export_name.size(), sizeof name - 1U);
    std::memcpy(name, export_name.data(), length);

    const std::array<std::uint32_t, 2> arguments{a, b};
    int missing = 0;
    const auto dispatched = g_dispatch_instance;
    g_dispatch_instance = self;
    const int result = mobagen_browser_wasm_call_export_js(self->handle(), name, heap_pointer(arguments.data()), 2, &missing);
    g_dispatch_instance = dispatched;
    if (missing != 0) return -1;
    *out_result = static_cast<std::uint32_t>(result);
    return 0;
  }

}  // namespace mobagen::plugins
