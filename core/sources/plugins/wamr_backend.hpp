#pragma once

#include "wasm_plugin_loader.hpp"

#include <memory>

namespace mobagen::plugins {

  /* Available only when Mobagen is configured with MOBAGEN_WASM_BACKEND_WAMR. */
  class WamrBackend final : public PortableWasmBackend {
  public:
    WamrBackend();
    ~WamrBackend() override;

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] PortableWasmInstantiationResult instantiate(std::span<const std::byte> binary,
                                                               std::shared_ptr<WasmHostImports> host_imports) override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
  };

}  // namespace mobagen::plugins
