#pragma once

#include "modules/lockfile.hpp"
#include "modules/manifest_parser.hpp"
#include "plugins/wasm_plugin_activation_set.hpp"
#include <mobagen/version.h>

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

  enum class PortableProjectLockPolicy : std::uint8_t { Ignore, Update, Frozen };

  struct PortableProjectLockOptions {
    PortableProjectLockPolicy policy{PortableProjectLockPolicy::Ignore};
    modules::SemanticVersion sdk_version{MOBAGEN_SDK_VERSION_MAJOR, MOBAGEN_SDK_VERSION_MINOR, MOBAGEN_SDK_VERSION_PATCH};
  };

  enum class PortableProjectIssueCode : std::uint8_t {
    ReadManifest,
    ParseManifest,
    Catalog,
    Resolution,
    LockMetadata,
    LockRead,
    LockMismatch,
    LockWrite,
    Activation,
  };

  struct PortableProjectIssue {
    PortableProjectIssueCode code{};
    std::string message;
    std::vector<modules::ManifestError> manifest_errors;
    std::vector<plugins::PortableWasmPluginCatalogIssue> catalog_issues;
    std::vector<modules::ResolutionIssue> resolution_issues;
    std::vector<modules::LockfileIssue> lockfile_issues;
    std::optional<modules::LockfileReadIssue> lockfile_read_issue;
    std::optional<modules::LockfileWriteIssue> lockfile_write_issue;
    std::vector<plugins::ResolvedPortableWasmPluginIssue> activation_issues;
  };

  struct PortableProjectPreview {
    modules::ProductDescriptor product;
    modules::CapabilityRegistry registry;
    modules::ModuleResolution resolution;
  };

  struct PortableProjectLockResult {
    std::filesystem::path lockfile_path;
    std::optional<std::string> contents;
    std::optional<PortableProjectPreview> preview;
    std::vector<PortableProjectIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return contents.has_value() && preview.has_value() && issues.empty(); }
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
    [[nodiscard]] modules::LockfileSerializeResult lockfile(modules::SemanticVersion sdk_version
                                                            = {MOBAGEN_SDK_VERSION_MAJOR, MOBAGEN_SDK_VERSION_MINOR,
                                                               MOBAGEN_SDK_VERSION_PATCH}) const;
    [[nodiscard]] plugins::PortableWasmPluginActivation* plugin(std::size_t index) noexcept;
    [[nodiscard]] const plugins::PortableWasmPluginActivation* plugin(std::size_t index) const noexcept;
    [[nodiscard]] plugins::ResolvedPortableWasmPluginActionResult stop();

  private:
    friend struct PortableProjectBuilder;
    friend PortableProjectResult load_portable_project(const std::filesystem::path&, modules::ResolverOptions, plugins::PortableWasmBackend&,
                                                       std::span<const modules::ProviderDescriptor>, PortableProjectLockOptions,
                                                       plugins::WasmHostServices);

    explicit PortableProjectRuntime(modules::ProductDescriptor product) : product_(std::move(product)) {}

    modules::ProductDescriptor product_;
    std::unique_ptr<plugins::PortableWasmPluginCatalog> catalog_;
    std::optional<modules::ModuleResolution> resolution_;
    modules::LockfileMetadata lockfile_metadata_;
    std::unique_ptr<plugins::ResolvedPortableWasmPluginActivation> activation_;
  };

  /* host_services.state must remain valid until this preview operation returns. */
  [[nodiscard]] PortableProjectLockResult resolve_portable_project_lock(
      const std::filesystem::path& manifest_path, modules::ResolverOptions options, plugins::PortableWasmBackend& backend,
      modules::SemanticVersion sdk_version = {MOBAGEN_SDK_VERSION_MAJOR, MOBAGEN_SDK_VERSION_MINOR, MOBAGEN_SDK_VERSION_PATCH},
      std::span<const modules::ProviderDescriptor> builtin_providers = {}, plugins::WasmHostServices host_services = {});

  /* The backend instances and host_services.state must remain valid for the lifetime of the project runtime. */
  [[nodiscard]] PortableProjectResult load_portable_project(const std::filesystem::path& manifest_path, modules::ResolverOptions options,
                                                            plugins::PortableWasmBackend& backend,
                                                            std::span<const modules::ProviderDescriptor> builtin_providers = {},
                                                            PortableProjectLockOptions lock_options = {},
                                                            plugins::WasmHostServices host_services = {});

}  // namespace mobagen::compositions
