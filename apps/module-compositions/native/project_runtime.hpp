#pragma once

#include "modules/lockfile.hpp"
#include "modules/manifest_parser.hpp"
#include "plugins/plugin_activation_set.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mobagen::compositions {

  enum class NativeProjectIssueCode : std::uint8_t {
    ReadManifest,
    ParseManifest,
    Catalog,
    Resolution,
    LockMetadata,
    Activation,
  };

  struct NativeProjectIssue {
    NativeProjectIssueCode code{};
    std::string message;
    std::vector<modules::ManifestError> manifest_errors;
    std::vector<plugins::NativePluginCatalogIssue> catalog_issues;
    std::vector<modules::ResolutionIssue> resolution_issues;
    std::vector<modules::LockfileIssue> lockfile_issues;
    std::vector<plugins::ResolvedNativePluginIssue> activation_issues;
  };

  class NativeProjectRuntime;

  struct NativeProjectResult {
    std::unique_ptr<NativeProjectRuntime> runtime;
    std::vector<NativeProjectIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return runtime != nullptr && issues.empty(); }
  };

  class NativeProjectRuntime {
  public:
    NativeProjectRuntime(const NativeProjectRuntime&) = delete;
    NativeProjectRuntime& operator=(const NativeProjectRuntime&) = delete;
    NativeProjectRuntime(NativeProjectRuntime&&) = delete;
    NativeProjectRuntime& operator=(NativeProjectRuntime&&) = delete;
    ~NativeProjectRuntime() = default;

    [[nodiscard]] const modules::ProductDescriptor& product() const noexcept { return product_; }
    [[nodiscard]] const modules::CapabilityRegistry& registry() const noexcept { return catalog_->registry(); }
    [[nodiscard]] const modules::ModuleResolution& resolution() const noexcept { return *resolution_; }
    [[nodiscard]] modules::LockfileSerializeResult lockfile(modules::SemanticVersion sdk_version) const;
    [[nodiscard]] plugins::PluginHost& host() noexcept { return host_; }
    [[nodiscard]] const plugins::PluginHost& host() const noexcept { return host_; }
    [[nodiscard]] plugins::ResolvedNativePluginActionResult stop();

  private:
    friend NativeProjectResult load_native_project(const std::filesystem::path&, modules::ResolverOptions,
                                                   std::span<const modules::ProviderDescriptor>);

    explicit NativeProjectRuntime(modules::ProductDescriptor product) : product_(std::move(product)) {}

    plugins::PluginHost host_;
    modules::ProductDescriptor product_;
    std::unique_ptr<plugins::NativePluginCatalog> catalog_;
    std::optional<modules::ModuleResolution> resolution_;
    modules::LockfileMetadata lockfile_metadata_;
    std::unique_ptr<plugins::ResolvedNativePluginActivation> activation_;
  };

  [[nodiscard]] NativeProjectResult load_native_project(const std::filesystem::path& manifest_path, modules::ResolverOptions options,
                                                        std::span<const modules::ProviderDescriptor> builtin_providers = {});

}  // namespace mobagen::compositions
