#pragma once

#include "wasm_plugin_loader.hpp"

#include <cstdint>
#include <memory>

namespace mobagen::plugins {

  inline constexpr std::uint32_t default_wamr_stack_size_bytes = 64U * 1024U;
  inline constexpr std::uint32_t default_wamr_max_memory_pages = 1024U;
  inline constexpr std::uint32_t max_wamr_stack_size_bytes = 8U * 1024U * 1024U;
  inline constexpr std::uint32_t max_wamr_memory_pages = 4096U;

  struct WamrBackendOptions {
    std::uint32_t stack_size_bytes{default_wamr_stack_size_bytes};
    std::uint32_t max_memory_pages{default_wamr_max_memory_pages};
  };

  /* Available only when Mobagen is configured with MOBAGEN_WASM_BACKEND_WAMR. */
  class WamrBackend final : public PortableWasmBackend {
  public:
    explicit WamrBackend(WamrBackendOptions options = {});
    ~WamrBackend() override;

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] PortableWasmInstantiationResult instantiate(std::span<const std::byte> binary,
                                                               std::shared_ptr<WasmHostImports> host_imports) override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
  };

}  // namespace mobagen::plugins
