#pragma once

#include "modules/capability_registry.hpp"
#include "modules/module_manifest.hpp"
#include "wasm_memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace mobagen::plugins {

  using WasmHostLogService = std::uint32_t (*)(void* state, std::uint32_t level, std::string_view message);
  using WasmHostSubmitCommandsService = std::uint32_t (*)(void* state, WasmCommandBatchView batch, std::span<const std::string> permissions);

  struct WasmHostServices {
    void* state{};
    WasmHostLogService log{};
    WasmHostSubmitCommandsService submit_commands{};
  };

  /*
   * Host-import marshaling descriptors (todo 10). The canonical guest imports
   * under module "mobagen_v1" (wasm_abi.h) are described by the SAME
   * descriptor vocabulary the manifest uses for exports — as their WASM-LEVEL
   * cell layout, since those imports are declared with flat u32 parameters:
   * every span the guest passes arrives as a flat (offset,size) i32 pair, not
   * as a pointer to a struct (that hidden-reference lowering only applies to
   * C-by-value struct EXPORT parameters). ONE walker
   * (dispatch_wasm_host_import) interprets cells -> typed call for BOTH
   * backends (WAMR native shims and the browser backend's JS import seam).
   *
   *   log              (i32 level, i32 message_offset, i32 message_size) -> i32
   *   find_capability  (i32 offset, i32 size, i32 version, ptr out_handle) -> i32
   *   submit_commands  (ptr input_batch, ptr result) -> i32
   */
  enum class WasmHostImportId : std::uint8_t { Log = 0, FindCapability = 1, SubmitCommands = 2 };

  struct WasmHostImportDescriptor {
    WasmHostImportId id;
    modules::ModuleExportMarshaling marshaling;
  };

  [[nodiscard]] inline constexpr std::array<WasmHostImportDescriptor, 3> wasm_host_import_descriptors() noexcept {
    return {{
        {WasmHostImportId::Log, {modules::ModuleMarshalingType::I32, 3,
                                 {modules::ModuleMarshalingType::I32, modules::ModuleMarshalingType::I32, modules::ModuleMarshalingType::I32}}},
        {WasmHostImportId::FindCapability,
         {modules::ModuleMarshalingType::I32, 4,
          {modules::ModuleMarshalingType::I32, modules::ModuleMarshalingType::I32, modules::ModuleMarshalingType::I32,
           modules::ModuleMarshalingType::Ptr}}},
        {WasmHostImportId::SubmitCommands,
         {modules::ModuleMarshalingType::I32, 2, {modules::ModuleMarshalingType::Ptr, modules::ModuleMarshalingType::Ptr}}},
    }};
  }

  namespace host_import_detail {
    inline constexpr auto wasm_host_import_descriptor_table = wasm_host_import_descriptors();
  }

  /* Returns a pointer with STATIC storage duration — the descriptor table
   * is a compile-time constant, never a per-call temporary. */
  [[nodiscard]] inline constexpr const WasmHostImportDescriptor* find_wasm_host_import_descriptor(WasmHostImportId id) noexcept {
    for (const auto& descriptor : host_import_detail::wasm_host_import_descriptor_table) {
      if (descriptor.id == id) return &descriptor;
    }
    return nullptr;
  }

  /*
   * Backend import shims call this object with the current guest memory view.
   * Service state must outlive every call. The bound registry is retained by
   * shared ownership after resolution has granted the provider permissions.
   */
  class WasmHostImports {
  public:
    explicit WasmHostImports(WasmHostServices services = {}) noexcept;
    WasmHostImports(const WasmHostImports&) = delete;
    WasmHostImports& operator=(const WasmHostImports&) = delete;
    WasmHostImports(WasmHostImports&&) = delete;
    WasmHostImports& operator=(WasmHostImports&&) = delete;

    [[nodiscard]] bool bind(std::shared_ptr<const modules::CapabilityRegistry> registry, std::span<const std::string> permissions);
    void unbind() noexcept;
    [[nodiscard]] bool bound() const noexcept { return static_cast<bool>(registry_); }
    [[nodiscard]] std::span<const std::string> permissions() const noexcept { return permissions_; }

    [[nodiscard]] std::uint32_t log(std::span<const std::byte> memory, std::uint32_t level, std::uint32_t message_offset,
                                    std::uint32_t message_size) const noexcept;
    [[nodiscard]] std::uint32_t find_capability(std::span<std::byte> memory, std::uint32_t capability_offset, std::uint32_t capability_size,
                                                std::uint32_t capability_version, std::uint32_t output_handle_offset) const noexcept;
    [[nodiscard]] std::uint32_t submit_commands(std::span<std::byte> memory, std::uint32_t input_batch_offset,
                                                std::uint32_t result_offset) const noexcept;

  private:
    [[nodiscard]] bool owner_thread() const noexcept { return owner_thread_ == std::this_thread::get_id(); }

    WasmHostServices services_;
    std::thread::id owner_thread_;
    std::shared_ptr<const modules::CapabilityRegistry> registry_;
    std::vector<std::string> permissions_;
  };

  /*
   * THE generic host-import dispatcher (todo 10): walks the canonical
   * descriptor for `id`, marshals raw u32 cells (i32/i64/f32/f64 pass
   * through; ptr bounds-checks an offset) against the guest linear memory,
   * then forwards the decoded typed call into `imports`. Shared by the WAMR
   * native shims and the browser backend's import seam — do not reimplement
   * per backend. Returns a MOBAGEN_WASM_STATUS_* code.
   */
  [[nodiscard]] std::uint32_t dispatch_wasm_host_import(const WasmHostImports& imports, WasmHostImportId id, std::span<std::byte> memory,
                                                        std::span<const std::uint32_t> cells) noexcept;

}  // namespace mobagen::plugins
