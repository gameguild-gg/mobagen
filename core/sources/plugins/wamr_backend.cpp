#include "wamr_backend.hpp"

/* WAMR is linked statically; its MSVC header otherwise assumes a DLL consumer. */
#define WASM_RUNTIME_API_EXTERN
#include <wasm_export.h>

#include <mobagen/plugin/wasm_abi.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mobagen::plugins {
  namespace {

    constexpr std::uint32_t wamr_stack_size = 64U * 1024U;
    constexpr std::size_t export_count = static_cast<std::size_t>(WasmPluginExport::Process) + 1U;
    constexpr std::size_t max_argument_cells = 8U;

    [[nodiscard]] std::span<std::byte> module_memory(wasm_module_inst_t module_instance) noexcept {
      if (module_instance == nullptr) return {};
      const auto memory = wasm_runtime_get_default_memory(module_instance);
      if (memory == nullptr) return {};

      const auto pages = wasm_memory_get_cur_page_count(memory);
      const auto bytes_per_page = wasm_memory_get_bytes_per_page(memory);
      if (bytes_per_page == 0U || pages > std::numeric_limits<std::uint64_t>::max() / bytes_per_page) return {};
      const auto byte_count = pages * bytes_per_page;
      if (byte_count > std::numeric_limits<std::size_t>::max()) return {};
      auto* base = static_cast<std::byte*>(wasm_memory_get_base_address(memory));
      if (base == nullptr && byte_count != 0U) return {};
      return {base, static_cast<std::size_t>(byte_count)};
    }

    [[nodiscard]] WasmHostImports* module_host_imports(wasm_module_inst_t module_instance) noexcept {
      if (module_instance == nullptr) return nullptr;
      return static_cast<WasmHostImports*>(wasm_runtime_get_custom_data(module_instance));
    }

    std::uint32_t host_log(wasm_exec_env_t execution_environment, std::uint32_t level, std::uint32_t message_offset,
                           std::uint32_t message_size) noexcept {
      if (execution_environment == nullptr) return MOBAGEN_WASM_STATUS_FAILED;
      const auto module_instance = wasm_runtime_get_module_inst(execution_environment);
      auto* imports = module_host_imports(module_instance);
      if (module_instance == nullptr || imports == nullptr) return MOBAGEN_WASM_STATUS_FAILED;
      return imports->log(module_memory(module_instance), level, message_offset, message_size);
    }

    std::uint32_t host_find_capability(wasm_exec_env_t execution_environment, std::uint32_t capability_offset,
                                      std::uint32_t capability_size, std::uint32_t capability_version,
                                      std::uint32_t output_handle_offset) noexcept {
      if (execution_environment == nullptr) return MOBAGEN_WASM_STATUS_FAILED;
      const auto module_instance = wasm_runtime_get_module_inst(execution_environment);
      auto* imports = module_host_imports(module_instance);
      if (module_instance == nullptr || imports == nullptr) return MOBAGEN_WASM_STATUS_FAILED;
      return imports->find_capability(module_memory(module_instance), capability_offset, capability_size, capability_version,
                                      output_handle_offset);
    }

    std::uint32_t host_submit_commands(wasm_exec_env_t execution_environment, std::uint32_t input_batch_offset,
                                       std::uint32_t result_offset) noexcept {
      if (execution_environment == nullptr) return MOBAGEN_WASM_STATUS_FAILED;
      const auto module_instance = wasm_runtime_get_module_inst(execution_environment);
      auto* imports = module_host_imports(module_instance);
      if (module_instance == nullptr || imports == nullptr) return MOBAGEN_WASM_STATUS_FAILED;
      return imports->submit_commands(module_memory(module_instance), input_batch_offset, result_offset);
    }

    std::array<NativeSymbol, 3>& host_symbols() {
      static std::array<NativeSymbol, 3> symbols{{
          {MOBAGEN_WASM_IMPORT_LOG_V1, reinterpret_cast<void*>(host_log), "(iii)i", nullptr},
          {MOBAGEN_WASM_IMPORT_FIND_CAPABILITY_V1, reinterpret_cast<void*>(host_find_capability), "(iiii)i", nullptr},
          {MOBAGEN_WASM_IMPORT_SUBMIT_COMMANDS_V1, reinterpret_cast<void*>(host_submit_commands), "(ii)i", nullptr},
      }};
      return symbols;
    }

    struct RuntimeLease;

    struct RuntimeRegistry {
      std::mutex mutex;
      std::weak_ptr<RuntimeLease> active;
    };

    RuntimeRegistry& runtime_registry() {
      static RuntimeRegistry registry;
      return registry;
    }

    struct RuntimeLease {
      ~RuntimeLease() {
        auto& registry = runtime_registry();
        const std::lock_guard lock{registry.mutex};
        wasm_runtime_destroy();
      }
    };

    std::shared_ptr<RuntimeLease> acquire_runtime(std::string& error) {
      auto& registry = runtime_registry();
      const std::lock_guard lock{registry.mutex};
      if (auto existing = registry.active.lock()) return existing;

      RuntimeInitArgs arguments{};
      arguments.mem_alloc_type = Alloc_With_System_Allocator;
      arguments.running_mode = Mode_Interp;
      auto& symbols = host_symbols();
      arguments.native_module_name = MOBAGEN_WASM_IMPORT_MODULE_V1;
      arguments.native_symbols = symbols.data();
      arguments.n_native_symbols = static_cast<std::uint32_t>(symbols.size());
      if (!wasm_runtime_full_init(&arguments)) {
        error = "WAMR runtime initialization failed";
        return {};
      }

      std::shared_ptr<RuntimeLease> lease;
      try {
        lease = std::make_shared<RuntimeLease>();
      } catch (const std::bad_alloc&) {
        wasm_runtime_destroy();
        error = "WAMR runtime lease allocation failed";
        return {};
      }
      registry.active = lease;
      return lease;
    }

    class WamrInstance final : public PortableWasmInstance {
    public:
      WamrInstance(std::vector<std::uint8_t> binary, wasm_module_t module, wasm_module_inst_t module_instance, wasm_exec_env_t execution_environment,
                   std::shared_ptr<RuntimeLease> runtime, std::shared_ptr<WasmHostImports> host_imports) noexcept
          : PortableWasmInstance(std::move(host_imports)),
            binary_(std::move(binary)),
            module_(module),
            module_instance_(module_instance),
            execution_environment_(execution_environment),
            runtime_(std::move(runtime)),
            owner_thread_(std::this_thread::get_id()) {
        wasm_runtime_set_custom_data(module_instance_, this->host_imports());
        for (std::size_t index = 0; index < exports_.size(); ++index) {
          const auto function = static_cast<WasmPluginExport>(index);
          const auto name = wasm_plugin_export_name(function);
          exports_[index] = wasm_runtime_lookup_function(module_instance_, name.data());
        }
      }

      ~WamrInstance() override {
        if (module_instance_ != nullptr) wasm_runtime_set_custom_data(module_instance_, nullptr);
        if (execution_environment_ != nullptr) wasm_runtime_destroy_exec_env(execution_environment_);
        if (module_instance_ != nullptr) wasm_runtime_deinstantiate(module_instance_);
        if (module_ != nullptr) wasm_runtime_unload(module_);
      }

      [[nodiscard]] WasmInvocationResult invoke(WasmPluginExport function, std::span<const std::uint32_t> arguments) override {
        if (std::this_thread::get_id() != owner_thread_) return WasmInvocationResult::failure("WAMR instance called outside its owner thread");
        if (arguments.size() > max_argument_cells) return WasmInvocationResult::failure("WAMR invocation has too many argument cells");

        const auto index = static_cast<std::size_t>(function);
        if (index >= exports_.size()) return WasmInvocationResult::failure("unknown Mobagen WASM export");
        const auto exported = exports_[index];
        if (exported == nullptr) return WasmInvocationResult::failure(std::string{wasm_plugin_export_name(function)} + " is not exported by WASM plugin");

        std::array<std::uint32_t, max_argument_cells> cells{};
        std::ranges::copy(arguments, cells.begin());
        wasm_runtime_clear_exception(module_instance_);
        if (!wasm_runtime_call_wasm(execution_environment_, exported, static_cast<std::uint32_t>(arguments.size()), cells.data())) {
          std::string error = "WAMR exception while invoking ";
          error += wasm_plugin_export_name(function);
          if (const char* exception = wasm_runtime_get_exception(module_instance_); exception != nullptr && *exception != '\0') {
            error += ": ";
            error += exception;
          }
          wasm_runtime_clear_exception(module_instance_);
          return WasmInvocationResult::failure(std::move(error));
        }
        return WasmInvocationResult::success(cells[0]);
      }

      [[nodiscard]] std::span<const std::byte> memory() const noexcept override {
        const auto view = mutable_memory();
        return {view.data(), view.size()};
      }

      [[nodiscard]] std::span<std::byte> writable_memory() noexcept override { return mutable_memory(); }

    private:
      [[nodiscard]] std::span<std::byte> mutable_memory() const noexcept {
        return module_memory(module_instance_);
      }

      std::vector<std::uint8_t> binary_;
      wasm_module_t module_{};
      wasm_module_inst_t module_instance_{};
      wasm_exec_env_t execution_environment_{};
      std::shared_ptr<RuntimeLease> runtime_;
      std::thread::id owner_thread_;
      std::array<wasm_function_inst_t, export_count> exports_{};
    };

  }  // namespace

  class WamrBackend::Impl {
  public:
    Impl() : runtime(acquire_runtime(error)) {}

    std::shared_ptr<RuntimeLease> runtime;
    std::string error;
  };

  WamrBackend::WamrBackend() : impl_(std::make_unique<Impl>()) {}

  WamrBackend::~WamrBackend() = default;

  bool WamrBackend::available() const noexcept { return impl_ != nullptr && impl_->runtime != nullptr; }

  PortableWasmInstantiationResult WamrBackend::instantiate(std::span<const std::byte> binary, std::shared_ptr<WasmHostImports> host_imports) {
    if (!available()) return PortableWasmInstantiationResult::failure(impl_ != nullptr ? impl_->error : "WAMR backend is unavailable");
    if (binary.empty()) return PortableWasmInstantiationResult::failure("WAMR cannot instantiate an empty module");
    if (binary.size() > std::numeric_limits<std::uint32_t>::max()) {
      return PortableWasmInstantiationResult::failure("WAMR module exceeds the 32-bit binary size limit");
    }

    std::vector<std::uint8_t> owned_binary;
    try {
      owned_binary.resize(binary.size());
    } catch (const std::bad_alloc&) {
      return PortableWasmInstantiationResult::failure("WAMR module copy ran out of memory");
    }
    std::memcpy(owned_binary.data(), binary.data(), binary.size());

    std::array<char, 512> error_buffer{};
    auto module = wasm_runtime_load(owned_binary.data(), static_cast<std::uint32_t>(owned_binary.size()), error_buffer.data(),
                                    static_cast<std::uint32_t>(error_buffer.size()));
    if (module == nullptr) return PortableWasmInstantiationResult::failure(std::string{"WAMR module load failed: "} + error_buffer.data());

    auto module_instance = wasm_runtime_instantiate(module, wamr_stack_size, 0U, error_buffer.data(), static_cast<std::uint32_t>(error_buffer.size()));
    if (module_instance == nullptr) {
      wasm_runtime_unload(module);
      return PortableWasmInstantiationResult::failure(std::string{"WAMR module instantiation failed: "} + error_buffer.data());
    }

    auto execution_environment = wasm_runtime_create_exec_env(module_instance, wamr_stack_size);
    if (execution_environment == nullptr) {
      wasm_runtime_deinstantiate(module_instance);
      wasm_runtime_unload(module);
      return PortableWasmInstantiationResult::failure("WAMR execution environment allocation failed");
    }

    try {
      auto instance = std::make_unique<WamrInstance>(std::move(owned_binary), module, module_instance, execution_environment,
                                                     impl_->runtime, std::move(host_imports));
      return PortableWasmInstantiationResult::success(std::move(instance));
    } catch (const std::bad_alloc&) {
      wasm_runtime_destroy_exec_env(execution_environment);
      wasm_runtime_deinstantiate(module_instance);
      wasm_runtime_unload(module);
      return PortableWasmInstantiationResult::failure("WAMR instance allocation failed");
    }
  }

}  // namespace mobagen::plugins
