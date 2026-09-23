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
     *
     * Todo 10: the guest's annotated exports are invoked through ONE generic
     * descriptor-driven dispatcher — no eval, no new Function, no generated
     * JS text. The JS side walks the marshaling type codes handed over as
     * plain integers: 1=i32, 2=i64, 3=f32, 4=f64, 5=ptr, 6=span. i32/f32
     * pass a single cell; i64/f64 read two cells and rebox via BigInt-free
     * bit casts (i64 through the wasm BigInt boundary when present, else as
     * a lo/hi pair — see call_typed_export_js); ptr and span marshal as
     * bounds-checked (offset[,size]) pairs against the guest linear memory.
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

    /*
     * Generic descriptor-driven export invocation (todo 10). codes_ptr
     * points at param_count marshaling type codes (1=i32,2=i64,3=f32,
     * 4=f64,5=ptr,6=span; the numbering IS module_abi.h's) and cells_ptr at
     * the raw u32 cell list. The JS walker mirrors the shared C++ decode
     * helper exactly: i32/f32/ptr/span each take one cell and pass through
     * (a span cell names a guest-memory (offset,size) pair — one i32 wasm
     * argument); i64 reads two cells and reboxes as BigInt.
     * No eval, no new Function: this body is fixed, only data varies.
     * result_cells_out receives up to two u32 result cells (i64/f64 results
     * arrive as lo/hi). Returns 0 ok, 1 missing export, 2 marshaling
     * failure.
     */
    EM_JS(int, mobagen_browser_wasm_call_typed_export_js,
           (int handle, const char* name_ptr, int param_count, unsigned int codes_ptr, unsigned int cells_ptr, int* missing_out,
            unsigned int result_cells_out),
           {
             var fn = globalThis.__mobagenBrowserWasm[handle].instance.exports[UTF8ToString(name_ptr)];
             if (typeof fn !== "function") {
               HEAPU32[missing_out >> 2] = 1;
               return 0;
             }
             HEAPU32[missing_out >> 2] = 0;
             var args = [];
             var cell = 0;
             for (var i = 0; i < param_count; ++i) {
               var code = HEAPU8[codes_ptr + i];
               if (code === 1 || code === 3 || code === 5) {
                 args.push(HEAPU32[(cells_ptr >> 2) + cell]);
                 cell += 1;
               } else if (code === 2) {
                 var lo = HEAPU32[(cells_ptr >> 2) + cell];
                 var hi = HEAPU32[(cells_ptr >> 2) + cell + 1];
                 cell += 2;
                 args.push((typeof BigInt === "function") ? (BigInt(hi >>> 0) << 32n) | BigInt(lo >>> 0) : lo);
               } else if (code === 4) {
                 var buf = new DataView(HEAPU8.buffer, ((cells_ptr >> 2) + cell) * 4, 8);
                 args.push(buf.getFloat64(0, true));
                 cell += 2;
               } else if (code === 6) {
                 /* span cell names the guest-memory (offset,size) pair; the
                    wasm32 convention passes it as ONE i32 argument. */
                 args.push(HEAPU32[(cells_ptr >> 2) + cell]);
                 cell += 1;
               } else {
                 return 2;
               }
             }
             /* A guest trap (todo 17: Lua's error path) surfaces as a JS
                RuntimeError — map it to the failure return (-5, trapped)
                instead of an uncaught exception killing the worker. The
                guest's own recovery protocol (drain export) collects the
                error afterwards. */
             var result;
             try {
               result = fn.apply(null, args);
             } catch (e) {
               if (e instanceof WebAssembly.RuntimeError) return -5;
               throw e;
             }
             if (result === undefined) {
               HEAPU32[result_cells_out >> 2] = 0;
               HEAPU32[(result_cells_out >> 2) + 1] = 0;
             } else if (typeof result === "bigint") {
               HEAPU32[result_cells_out >> 2] = Number(result & 0xffffffffn) >>> 0;
               HEAPU32[(result_cells_out >> 2) + 1] = Number((result >> 32n) & 0xffffffffn) >>> 0;
             } else if (typeof result === "number") {
               /* i32/f32 returns land in cell 0 as their raw u32; a Number
                 that is not integral must be an f64 (two cells, bit pattern). */
               if (Number.isInteger(result) && result >= -2147483648 && result <= 4294967295) {
                 HEAPU32[result_cells_out >> 2] = result >>> 0;
                 HEAPU32[(result_cells_out >> 2) + 1] = 0;
               } else {
                 var view = new DataView(HEAPU8.buffer, result_cells_out, 8);
                 view.setFloat64(0, result, true);
               }
             } else {
               return 2;
             }
             return 0;
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

      /* Todo 10 descriptor-dispatch accessors: the typed invoke path
         bounds-checks cells against the current mirror (host writes staged
         through writable_memory are visible and must not be refreshed away)
         and flushes pending mirror writes before entering the guest. */
      [[nodiscard]] std::size_t mirror_bytes() const noexcept { return mirror_.size(); }
      [[nodiscard]] bool has_unflushed_writes() const noexcept { return mirror_dirty_; }
      [[nodiscard]] bool flush_mirror_for_invoke() const noexcept { return flush_mirror(); }

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
   * Host-import dispatch seam, descriptor-driven since todo 10: the canonical
   * WasmHostImportId table in wasm_host_imports.hpp describes each import's
   * marshaling; the raw argument cells (up to four here — the widest canonical
   * import, find_capability, is span+i32+ptr = 4 cells) are walked by the
   * shared dispatch_wasm_host_import exactly like the WAMR native shims do,
   * against the mirror view of the guest linear memory, then flushed back so
   * writes (find_capability handle, submit_commands result) become visible to
   * the running guest.
   */
  extern "C" EMSCRIPTEN_KEEPALIVE int mobagen_browser_wasm_dispatch_c(int function, unsigned int a, unsigned int b, unsigned int c,
                                                                      unsigned int d) {
    auto* const instance = g_dispatch_instance;
    if (instance == nullptr) return MOBAGEN_WASM_STATUS_FAILED;
    const auto* const imports = instance->host_imports();
    if (imports == nullptr) return MOBAGEN_WASM_STATUS_FAILED;

    const std::array<std::uint32_t, 4> cells{a, b, c, d};
    const auto memory = instance->writable_memory();
    const auto status = dispatch_wasm_host_import(*imports, static_cast<WasmHostImportId>(function), memory, cells);
    instance->flush_mirror_for_dispatch();
    return static_cast<int>(status);
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

  int browser_wasm_invoke_typed_export(PortableWasmInstance& instance, std::string_view export_name,
                                       const modules::ModuleExportMarshaling& marshaling, std::span<const std::uint32_t> cells,
                                       std::array<std::uint32_t, 2>& out_result_cells) noexcept {
    auto* const self = registered_instance(&instance);
    if (self == nullptr) return -1;
    if (marshaling.param_count > modules::module_abi_max_export_params) return -2;
    /* Bounds-check against the CURRENT mirror state: the caller may have
     * staged span structs / ptr targets through writable_memory(), and a
     * refresh here would overwrite those host writes before the flush. */
    if (!modules::module_marshaling_cells_in_bounds(marshaling, cells, self->mirror_bytes())) return -3;

    char name[128]{};
    const auto length = std::min(export_name.size(), sizeof name - 1U);
    std::memcpy(name, export_name.data(), length);

    std::array<unsigned char, modules::module_abi_max_export_params> codes{};
    for (std::uint32_t index = 0; index < marshaling.param_count; ++index) {
      codes[index] = static_cast<unsigned char>(marshaling.params[index]);
    }

    if (self->has_unflushed_writes() && !self->flush_mirror_for_invoke()) return -4;

    std::array<std::uint32_t, 8> cell_buffer{};
    std::ranges::copy(cells, cell_buffer.begin());
    out_result_cells = {};

    int missing = 0;
    const auto dispatched = g_dispatch_instance;
    g_dispatch_instance = self;
    const int status = mobagen_browser_wasm_call_typed_export_js(self->handle(), name, static_cast<int>(marshaling.param_count),
                                                                 heap_pointer(codes.data()), heap_pointer(cell_buffer.data()), &missing,
                                                                 heap_pointer(out_result_cells.data()));
    g_dispatch_instance = dispatched;
    if (status == 2) return -2;
    if (status == -5) return -5; /* guest trap (todo 17 recovery protocol) */
    if (missing != 0) return -1;
    return 0;
  }

}  // namespace mobagen::plugins
