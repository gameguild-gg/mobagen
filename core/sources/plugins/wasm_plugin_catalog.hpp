#pragma once

#include "modules/capability_registry.hpp"
#include "wasm_plugin_loader.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mobagen::plugins {

  enum class PortableWasmPluginCatalogIssueCode : std::uint8_t {
    InvalidProduct,
    InvalidProjectRoot,
    PathOutsideProject,
    DuplicatePackage,
    LoadFailed,
    RegistryFailed,
  };

  struct PortableWasmPluginCatalogIssue {
    PortableWasmPluginCatalogIssueCode code{};
    std::filesystem::path path;
    std::string message;
    std::vector<modules::DescriptorIssue> descriptor_issues;
    std::vector<PortableWasmPluginLoadIssue> load_issues;
    std::vector<modules::RegistryIssue> registry_issues;
  };

  class PortableWasmPluginCatalog;

  struct PortableWasmPluginCatalogResult {
    std::unique_ptr<PortableWasmPluginCatalog> catalog;
    std::vector<PortableWasmPluginCatalogIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return catalog != nullptr && issues.empty(); }
  };

  class PortableWasmPluginCatalog {
  public:
    PortableWasmPluginCatalog(const PortableWasmPluginCatalog&) = delete;
    PortableWasmPluginCatalog& operator=(const PortableWasmPluginCatalog&) = delete;
    PortableWasmPluginCatalog(PortableWasmPluginCatalog&&) = delete;
    PortableWasmPluginCatalog& operator=(PortableWasmPluginCatalog&&) = delete;

    [[nodiscard]] std::size_t plugin_count() const noexcept { return plugins_.size(); }
    [[nodiscard]] const LoadedPortableWasmPlugin* plugin(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<LoadedPortableWasmPlugin> take_plugin(std::string_view provider_id);
    [[nodiscard]] bool is_plugin_provider(std::string_view provider_id) const noexcept;
    void discard_plugins() noexcept { plugins_.clear(); }
    [[nodiscard]] const modules::CapabilityRegistry& registry() const noexcept { return registry_; }

  private:
    friend PortableWasmPluginCatalogResult discover_portable_wasm_plugin_catalog(const modules::ProductDescriptor&, const std::filesystem::path&,
                                                                                 PortableWasmBackend&, std::span<const modules::ProviderDescriptor>,
                                                                                 WasmHostServices);

    PortableWasmPluginCatalog(std::vector<LoadedPortableWasmPlugin> plugins, modules::CapabilityRegistry registry);

    std::vector<LoadedPortableWasmPlugin> plugins_;
    std::vector<std::string> plugin_provider_ids_;
    modules::CapabilityRegistry registry_;
  };

  /* host_services.state must remain valid until the catalog and every activation created from it are destroyed. */
  [[nodiscard]] PortableWasmPluginCatalogResult discover_portable_wasm_plugin_catalog(
      const modules::ProductDescriptor& product, const std::filesystem::path& project_root, PortableWasmBackend& backend,
      std::span<const modules::ProviderDescriptor> builtin_providers = {}, WasmHostServices host_services = {});

}  // namespace mobagen::plugins
