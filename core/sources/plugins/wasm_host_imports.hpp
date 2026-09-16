#pragma once

#include "modules/capability_registry.hpp"
#include "wasm_memory.hpp"

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

}  // namespace mobagen::plugins
