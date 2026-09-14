#pragma once

#include "modules/capability_registry.hpp"
#include "plugin_host.hpp"
#include "plugin_loader.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mobagen::plugins {

  enum class NativePluginCatalogIssueCode : std::uint8_t {
    InvalidProduct,
    InvalidProjectRoot,
    PathOutsideProject,
    DuplicatePackage,
    LoadFailed,
    RegistryFailed,
  };

  struct NativePluginCatalogIssue {
    NativePluginCatalogIssueCode code{};
    std::filesystem::path path;
    std::string message;
    std::vector<modules::DescriptorIssue> descriptor_issues;
    std::vector<NativePluginLoadIssue> load_issues;
    std::vector<modules::RegistryIssue> registry_issues;
  };

  class NativePluginCatalog;

  struct NativePluginCatalogResult {
    std::unique_ptr<NativePluginCatalog> catalog;
    std::vector<NativePluginCatalogIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return catalog != nullptr && issues.empty(); }
  };

  class NativePluginCatalog {
  public:
    NativePluginCatalog(const NativePluginCatalog&) = delete;
    NativePluginCatalog& operator=(const NativePluginCatalog&) = delete;
    NativePluginCatalog(NativePluginCatalog&&) = delete;
    NativePluginCatalog& operator=(NativePluginCatalog&&) = delete;

    [[nodiscard]] std::size_t plugin_count() const noexcept { return plugins_.size(); }
    [[nodiscard]] const NativePlugin* plugin(std::size_t index) const noexcept;
    [[nodiscard]] const modules::CapabilityRegistry& registry() const noexcept { return registry_; }

  private:
    friend NativePluginCatalogResult discover_native_plugin_catalog(const modules::ProductDescriptor&, const std::filesystem::path&, PluginHost&,
                                                                    std::span<const modules::ProviderDescriptor>);

    NativePluginCatalog(std::vector<NativePlugin> plugins, modules::CapabilityRegistry registry);

    std::vector<NativePlugin> plugins_;
    modules::CapabilityRegistry registry_;
  };

  [[nodiscard]] NativePluginCatalogResult discover_native_plugin_catalog(const modules::ProductDescriptor& product,
                                                                         const std::filesystem::path& project_root, PluginHost& host,
                                                                         std::span<const modules::ProviderDescriptor> builtin_providers = {});

}  // namespace mobagen::plugins
