#pragma once

#include "wasm_plugin_loader.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mobagen::plugins {

  inline constexpr std::uint32_t default_wamr_stack_size_bytes = 64U * 1024U;
  inline constexpr std::uint32_t default_wamr_max_memory_pages = 1024U;
  inline constexpr std::uint32_t max_wamr_stack_size_bytes = 8U * 1024U * 1024U;
  inline constexpr std::uint32_t max_wamr_memory_pages = 4096U;

  /* WAMR shared heaps live at the top of the wasm address space: at most 1 GiB. */
  inline constexpr std::uint32_t max_wamr_shared_heap_bytes = 1024U * 1024U * 1024U;

  struct WamrBackendOptions {
    std::uint32_t stack_size_bytes{default_wamr_stack_size_bytes};
    std::uint32_t max_memory_pages{default_wamr_max_memory_pages};
    /* When non-zero the backend owns one shared heap region of this size (page
     * aligned) that every instantiated module attaches; 0 keeps the backend
     * shared-heap free. */
    std::uint32_t shared_heap_size_bytes{0};
  };

  /* The host-visible view of the backend's shared heap region. Consumed by the
   * native memory shim (todo 13). Null/empty before heap creation. */
  struct WamrSharedHeapRegion {
    std::byte* data{nullptr};
    std::size_t size{0};
  };

  /* Available only when Mobagen is configured with MOBAGEN_WASM_BACKEND_WAMR. */
  class WamrBackend final : public PortableWasmBackend, public AotAwarePortableWasmBackend {
  public:
    explicit WamrBackend(WamrBackendOptions options = {});
    ~WamrBackend() override;

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] PortableWasmInstantiationResult instantiate(std::span<const std::byte> binary,
                                                               std::shared_ptr<WasmHostImports> host_imports) override;
    [[nodiscard]] PortableWasmAotSelection instantiate_prefer_aot(std::span<const std::byte> wasm_binary,
                                                                  std::span<const std::byte> aot_binary,
                                                                  std::string_view aot_toolchain_version,
                                                                  std::shared_ptr<WasmHostImports> host_imports) override;

    /* WAMR runtime version (the pin, e.g. 2.4.5) — the value .aot manifests'
     * toolchain.version must match. */
    [[nodiscard]] static std::string_view runtime_wamr_version() noexcept;

    /* Shared heap region shared by all instances of this backend; empty when
     * shared_heap_size_bytes is 0. The span stays valid for the backend's
     * lifetime. Consumed by todo 13's native memory shim. */
    [[nodiscard]] WamrSharedHeapRegion shared_heap_region() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;

    /* Shared by instantiate() and instantiate_prefer_aot(): loads, validates,
     * attaches the shared heap (if configured), and creates the instance. */
    [[nodiscard]] PortableWasmInstantiationResult instantiate_loaded(std::shared_ptr<WasmHostImports> host_imports,
                                                                     std::span<const std::byte> binary);
  };

}  // namespace mobagen::plugins
