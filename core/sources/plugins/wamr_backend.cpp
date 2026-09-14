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
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace mobagen::plugins {
  namespace {

    constexpr std::size_t export_count = static_cast<std::size_t>(WasmPluginExport::Process) + 1U;
    constexpr std::size_t max_argument_cells = 8U;

    [[nodiscard]] bool read_u32_leb(std::span<const std::byte> bytes, std::size_t& cursor, std::uint32_t& value) noexcept {
      value = 0;
      for (std::uint32_t octet_index = 0; octet_index < 5U; ++octet_index) {
        if (cursor == bytes.size()) return false;
        const auto octet = std::to_integer<std::uint8_t>(bytes[cursor++]);
        const auto payload = static_cast<std::uint32_t>(octet & 0x7fU);
        if (octet_index == 4U && payload > 0x0fU) return false;
        value |= payload << (octet_index * 7U);
        if ((octet & 0x80U) == 0U) return true;
      }
      return false;
    }

    [[nodiscard]] bool validate_binary_memory_budget(std::span<const std::byte> binary, std::uint32_t max_memory_pages,
                                                     std::string& error) noexcept {
      constexpr std::size_t wasm_header_size = 8U;
      constexpr std::uint8_t memory_section_id = 5U;
      if (binary.size() < wasm_header_size) return true;

      bool found_memory = false;
      std::size_t cursor = wasm_header_size;
      while (cursor < binary.size()) {
        const auto section_id = std::to_integer<std::uint8_t>(binary[cursor++]);
        std::uint32_t section_size = 0;
        if (!read_u32_leb(binary, cursor, section_size) || section_size > binary.size() - cursor) {
          error = "WAMR module has malformed section framing before its memory declaration";
          return false;
        }
        const auto section_end = cursor + section_size;
        if (section_id != memory_section_id) {
          cursor = section_end;
          continue;
        }
        if (found_memory) {
          error = "WAMR module contains duplicate memory sections";
          return false;
        }
        found_memory = true;

        std::uint32_t memory_count = 0;
        std::uint32_t flags = 0;
        std::uint32_t initial_pages = 0;
        if (!read_u32_leb(binary.first(section_end), cursor, memory_count) || memory_count != 1U
            || !read_u32_leb(binary.first(section_end), cursor, flags) || (flags & ~1U) != 0U
            || !read_u32_leb(binary.first(section_end), cursor, initial_pages)) {
          error = "WAMR module must declare one unshared 32-bit linear memory";
          return false;
        }
        if (initial_pages > max_memory_pages) {
          error = "WAMR module initial linear memory exceeds the configured memory budget";
          return false;
        }
        if ((flags & 1U) != 0U) {
          std::uint32_t maximum_pages = 0;
          if (!read_u32_leb(binary.first(section_end), cursor, maximum_pages) || maximum_pages < initial_pages) {
            error = "WAMR module has malformed linear memory limits";
            return false;
          }
        }
        if (cursor != section_end) {
          error = "WAMR module has trailing data in its memory section";
          return false;
        }
      }
      if (!found_memory) {
        error = "WAMR module must declare a linear memory";
        return false;
      }
      return true;
    }

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

    [[nodiscard]] bool validate_module_memory(wasm_module_t module, std::uint32_t max_memory_pages,
                                              std::uint32_t& instantiation_memory_pages, std::string& error) {
      const auto export_total = wasm_runtime_get_export_count(module);
      if (export_total < 0) {
        error = "WAMR module memory exports could not be inspected";
        return false;
      }
      for (std::int32_t index = 0; index < export_total; ++index) {
        wasm_export_t exported{};
        wasm_runtime_get_export_type(module, index, &exported);
        if (exported.name == nullptr || std::string_view{exported.name} != MOBAGEN_WASM_MEMORY_EXPORT_V1
            || exported.kind != WASM_IMPORT_EXPORT_KIND_MEMORY) {
          continue;
        }
        if (exported.u.memory_type == nullptr) {
          error = "WAMR module has an invalid linear memory export";
          return false;
        }
        if (wasm_memory_type_get_shared(exported.u.memory_type)) {
          error = "WAMR module requests unsupported shared linear memory";
          return false;
        }
        if (wasm_memory_type_get_init_page_count(exported.u.memory_type) > max_memory_pages) {
          error = "WAMR module initial linear memory exceeds the configured memory budget";
          return false;
        }
        const auto module_maximum = wasm_memory_type_get_max_page_count(exported.u.memory_type);
        instantiation_memory_pages = module_maximum == 0U ? max_memory_pages : std::min(max_memory_pages, module_maximum);
        return true;
      }
      error = "WAMR module must export its default linear memory as 'memory'";
      return false;
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
    explicit Impl(WamrBackendOptions configured) : options(configured) {
      if (options.stack_size_bytes == 0U || options.stack_size_bytes > max_wamr_stack_size_bytes || options.max_memory_pages == 0U
          || options.max_memory_pages > max_wamr_memory_pages) {
        error = "WAMR backend options exceed the supported stack or memory budget";
        return;
      }
      runtime = acquire_runtime(error);
    }

    WamrBackendOptions options;
    std::shared_ptr<RuntimeLease> runtime;
    std::string error;
  };

  WamrBackend::WamrBackend(WamrBackendOptions options) : impl_(std::make_unique<Impl>(options)) {}

  WamrBackend::~WamrBackend() = default;

  bool WamrBackend::available() const noexcept { return impl_ != nullptr && impl_->runtime != nullptr; }

  PortableWasmInstantiationResult WamrBackend::instantiate(std::span<const std::byte> binary, std::shared_ptr<WasmHostImports> host_imports) {
    if (!available()) return PortableWasmInstantiationResult::failure(impl_ != nullptr ? impl_->error : "WAMR backend is unavailable");
    if (binary.empty()) return PortableWasmInstantiationResult::failure("WAMR cannot instantiate an empty module");
    if (binary.size() > std::numeric_limits<std::uint32_t>::max()) {
      return PortableWasmInstantiationResult::failure("WAMR module exceeds the 32-bit binary size limit");
    }
    std::string memory_error;
    if (!validate_binary_memory_budget(binary, impl_->options.max_memory_pages, memory_error)) {
      return PortableWasmInstantiationResult::failure(std::move(memory_error));
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

    memory_error.clear();
    auto instantiation_memory_pages = impl_->options.max_memory_pages;
    if (!validate_module_memory(module, impl_->options.max_memory_pages, instantiation_memory_pages, memory_error)) {
      wasm_runtime_unload(module);
      return PortableWasmInstantiationResult::failure(std::move(memory_error));
    }

    InstantiationArgs instantiation{};
    instantiation.default_stack_size = impl_->options.stack_size_bytes;
    instantiation.host_managed_heap_size = 0U;
    instantiation.max_memory_pages = instantiation_memory_pages;
    auto module_instance
        = wasm_runtime_instantiate_ex(module, &instantiation, error_buffer.data(), static_cast<std::uint32_t>(error_buffer.size()));
    if (module_instance == nullptr) {
      wasm_runtime_unload(module);
      return PortableWasmInstantiationResult::failure(std::string{"WAMR module instantiation failed: "} + error_buffer.data());
    }

    auto execution_environment = wasm_runtime_create_exec_env(module_instance, impl_->options.stack_size_bytes);
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
