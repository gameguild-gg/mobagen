#pragma once

#include "modules/resolver.hpp"
#include "wasm_plugin_catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mobagen::plugins {

  enum class ResolvedPortableWasmPluginIssueCode : std::uint8_t {
    InvalidResolution,
    PluginUnavailable,
    ActivationFailed,
    ShutdownFailed,
  };

  struct ResolvedPortableWasmPluginIssue {
    ResolvedPortableWasmPluginIssueCode code{};
    std::string provider_id;
    std::string message;
    std::vector<PortableWasmPluginIssue> activation_issues;
  };

  struct ResolvedPortableWasmPluginActionResult {
    std::vector<ResolvedPortableWasmPluginIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  struct ResolvedPortableWasmPluginActivationResult;

  class ResolvedPortableWasmPluginActivation {
  public:
    ResolvedPortableWasmPluginActivation(const ResolvedPortableWasmPluginActivation&) = delete;
    ResolvedPortableWasmPluginActivation& operator=(const ResolvedPortableWasmPluginActivation&) = delete;
    ResolvedPortableWasmPluginActivation(ResolvedPortableWasmPluginActivation&&) = delete;
    ResolvedPortableWasmPluginActivation& operator=(ResolvedPortableWasmPluginActivation&&) = delete;
    ~ResolvedPortableWasmPluginActivation();

    [[nodiscard]] std::size_t size() const noexcept { return activations_.size(); }
    [[nodiscard]] PortableWasmPluginActivation* plugin(std::size_t index) noexcept;
    [[nodiscard]] const PortableWasmPluginActivation* plugin(std::size_t index) const noexcept;
    [[nodiscard]] ResolvedPortableWasmPluginActionResult stop();

  private:
    friend ResolvedPortableWasmPluginActivationResult activate_resolved_portable_wasm_plugins(PortableWasmPluginCatalog&,
                                                                                              const modules::ModuleResolution&);

    explicit ResolvedPortableWasmPluginActivation(std::vector<std::unique_ptr<PortableWasmPluginActivation>> activations);

    std::vector<std::unique_ptr<PortableWasmPluginActivation>> activations_;
  };

  struct ResolvedPortableWasmPluginActivationResult {
    std::unique_ptr<ResolvedPortableWasmPluginActivation> activation;
    std::vector<ResolvedPortableWasmPluginIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return activation != nullptr && issues.empty(); }
  };

  [[nodiscard]] ResolvedPortableWasmPluginActivationResult activate_resolved_portable_wasm_plugins(PortableWasmPluginCatalog& catalog,
                                                                                                   const modules::ModuleResolution& resolution);

}  // namespace mobagen::plugins
