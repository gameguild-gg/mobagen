#pragma once

#include "modules/resolver.hpp"
#include "plugin_activation.hpp"
#include "plugin_catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mobagen::plugins {

  enum class ResolvedNativePluginIssueCode : std::uint8_t {
    InvalidResolution,
    ActivationFailed,
    ShutdownFailed,
  };

  struct ResolvedNativePluginIssue {
    ResolvedNativePluginIssueCode code{};
    std::string provider_id;
    std::string message;
    std::vector<NativePluginActivationIssue> activation_issues;
  };

  struct ResolvedNativePluginActionResult {
    std::vector<ResolvedNativePluginIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  struct ResolvedNativePluginActivationResult;

  class ResolvedNativePluginActivation {
  public:
    ResolvedNativePluginActivation(const ResolvedNativePluginActivation&) = delete;
    ResolvedNativePluginActivation& operator=(const ResolvedNativePluginActivation&) = delete;
    ResolvedNativePluginActivation(ResolvedNativePluginActivation&&) = delete;
    ResolvedNativePluginActivation& operator=(ResolvedNativePluginActivation&&) = delete;
    ~ResolvedNativePluginActivation();

    [[nodiscard]] std::size_t size() const noexcept { return activations_.size(); }
    [[nodiscard]] ResolvedNativePluginActionResult stop();

  private:
    friend ResolvedNativePluginActivationResult activate_resolved_native_plugins(NativePluginCatalog&, const modules::ModuleResolution&, PluginHost&);

    explicit ResolvedNativePluginActivation(std::vector<std::unique_ptr<NativePluginActivation>> activations);

    std::vector<std::unique_ptr<NativePluginActivation>> activations_;
  };

  struct ResolvedNativePluginActivationResult {
    std::unique_ptr<ResolvedNativePluginActivation> activation;
    std::vector<ResolvedNativePluginIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return activation != nullptr && issues.empty(); }
  };

  [[nodiscard]] ResolvedNativePluginActivationResult activate_resolved_native_plugins(NativePluginCatalog& catalog,
                                                                                      const modules::ModuleResolution& resolution, PluginHost& host);

}  // namespace mobagen::plugins
