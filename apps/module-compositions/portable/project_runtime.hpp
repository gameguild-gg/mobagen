#pragma once

#include "modules/manifest_parser.hpp"
#include "plugins/wasm_plugin_activation_set.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mobagen::compositions {

  enum class PortableProjectIssueCode : std::uint8_t {
    ReadManifest,
    ParseManifest,
    Catalog,
    Resolution,
    Activation,
  };

  struct PortableProjectIssue {
    PortableProjectIssueCode code{};
    std::string message;
    std::vector<modules::ManifestError> manifest_errors;
    std::vector<plugins::PortableWasmPluginCatalogIssue> catalog_issues;
    std::vector<modules::ResolutionIssue> resolution_issues;
    std::vector<plugins::ResolvedPortableWasmPluginIssue> activation_issues;
  };

  class PortableProjectRuntime;

  struct PortableProjectResult {
    std::unique_ptr<PortableProjectRuntime> runtime;
    std::vector<PortableProjectIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return runtime != nullptr && issues.empty(); }
  };

  class PortableProjectRuntime {
  public:
    PortableProjectRuntime(const PortableProjectRuntime&) = delete;
    PortableProjectRuntime& operator=(const PortableProjectRuntime&) = delete;
    PortableProjectRuntime(PortableProjectRuntime&&) = delete;
    PortableProjectRuntime& operator=(PortableProjectRuntime&&) = delete;
    ~PortableProjectRuntime() = default;

    [[nodiscard]] const modules::ProductDescriptor& product() const noexcept { return product_; }
    [[nodiscard]] const modules::CapabilityRegistry& registry() const noexcept { return catalog_->registry(); }
    [[nodiscard]] const modules::ModuleResolution& resolution() const noexcept { return *resolution_; }
    [[nodiscard]] plugins::PortableWasmPluginActivation* plugin(std::size_t index) noexcept;
    [[nodiscard]] const plugins::PortableWasmPluginActivation* plugin(std::size_t index) const noexcept;
    [[nodiscard]] plugins::ResolvedPortableWasmPluginActionResult stop();

  private:
    friend PortableProjectResult load_portable_project(const std::filesystem::path&, modules::ResolverOptions, plugins::PortableWasmBackend&,
                                                       std::span<const modules::ProviderDescriptor>);

    explicit PortableProjectRuntime(modules::ProductDescriptor product) : product_(std::move(product)) {}

    modules::ProductDescriptor product_;
    std::unique_ptr<plugins::PortableWasmPluginCatalog> catalog_;
    std::optional<modules::ModuleResolution> resolution_;
    std::unique_ptr<plugins::ResolvedPortableWasmPluginActivation> activation_;
  };

  /* The backend must keep every returned instance valid for the lifetime of the project runtime. */
  [[nodiscard]] PortableProjectResult load_portable_project(const std::filesystem::path& manifest_path, modules::ResolverOptions options,
                                                            plugins::PortableWasmBackend& backend,
                                                            std::span<const modules::ProviderDescriptor> builtin_providers = {});

}  // namespace mobagen::compositions
