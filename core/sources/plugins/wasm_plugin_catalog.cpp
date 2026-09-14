#include "wasm_plugin_catalog.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

namespace mobagen::plugins {
  namespace {

    void add_issue(PortableWasmPluginCatalogResult& result, PortableWasmPluginCatalogIssueCode code, std::filesystem::path path, std::string message,
                   std::vector<modules::DescriptorIssue> descriptor_issues = {}, std::vector<PortableWasmPluginLoadIssue> load_issues = {},
                   std::vector<modules::RegistryIssue> registry_issues = {}) {
      result.issues.push_back(
          {code, std::move(path), std::move(message), std::move(descriptor_issues), std::move(load_issues), std::move(registry_issues)});
    }

    [[nodiscard]] bool is_inside(const std::filesystem::path& root, const std::filesystem::path& candidate) {
      const auto relative = candidate.lexically_relative(root);
      if (relative.empty() || relative.is_absolute()) {
        return false;
      }
      return std::ranges::none_of(relative, [](const auto& component) { return component == ".."; });
    }

    [[nodiscard]] bool is_duplicate(const std::filesystem::path& candidate, std::span<const std::filesystem::path> existing) {
      for (const auto& path : existing) {
        if (path == candidate) {
          return true;
        }
        std::error_code error;
        if (std::filesystem::equivalent(path, candidate, error) && !error) {
          return true;
        }
      }
      return false;
    }

  }  // namespace

  PortableWasmPluginCatalog::PortableWasmPluginCatalog(std::vector<LoadedPortableWasmPlugin> plugins, modules::CapabilityRegistry registry)
      : plugins_(std::move(plugins)), registry_(std::move(registry)) {
    plugin_provider_ids_.reserve(plugins_.size());
    for (const auto& plugin : plugins_) plugin_provider_ids_.push_back(plugin.provider().id);
    std::ranges::sort(plugin_provider_ids_);
  }

  const LoadedPortableWasmPlugin* PortableWasmPluginCatalog::plugin(std::size_t index) const noexcept {
    return index < plugins_.size() ? &plugins_[index] : nullptr;
  }

  std::optional<LoadedPortableWasmPlugin> PortableWasmPluginCatalog::take_plugin(std::string_view provider_id) {
    const auto found
        = std::ranges::find_if(plugins_, [provider_id](const LoadedPortableWasmPlugin& plugin) { return plugin.provider().id == provider_id; });
    if (found == plugins_.end()) return std::nullopt;
    std::optional<LoadedPortableWasmPlugin> result{std::move(*found)};
    plugins_.erase(found);
    return result;
  }

  bool PortableWasmPluginCatalog::is_plugin_provider(std::string_view provider_id) const noexcept {
    return std::ranges::binary_search(plugin_provider_ids_, provider_id);
  }

  PortableWasmPluginCatalogResult discover_portable_wasm_plugin_catalog(const modules::ProductDescriptor& product,
                                                                        const std::filesystem::path& project_root, PortableWasmBackend& backend,
                                                                        std::span<const modules::ProviderDescriptor> builtin_providers,
                                                                        WasmHostServices host_services) {
    PortableWasmPluginCatalogResult result;
    auto descriptor_issues = modules::validate(product);
    if (!descriptor_issues.empty()) {
      add_issue(result, PortableWasmPluginCatalogIssueCode::InvalidProduct, {}, "product descriptor is invalid", std::move(descriptor_issues));
      return result;
    }

    std::error_code error;
    const auto root_status = std::filesystem::status(project_root, error);
    if (error || !std::filesystem::is_directory(root_status)) {
      add_issue(result, PortableWasmPluginCatalogIssueCode::InvalidProjectRoot, project_root, "plugin project root must be a readable directory");
      return result;
    }
    const auto canonical_root = std::filesystem::canonical(project_root, error);
    if (error) {
      add_issue(result, PortableWasmPluginCatalogIssueCode::InvalidProjectRoot, project_root, "plugin project root could not be canonicalized");
      return result;
    }

    std::vector<std::filesystem::path> packages;
    packages.reserve(product.plugins.size());
    for (const auto& declared : product.plugins) {
      const std::filesystem::path declared_path{declared};
      if (declared_path.is_absolute()) {
        add_issue(result, PortableWasmPluginCatalogIssueCode::PathOutsideProject, declared_path,
                  "plugin package path must be relative to mobagen.yaml");
        continue;
      }
      error.clear();
      const auto package = std::filesystem::weakly_canonical(canonical_root / declared_path, error);
      if (error || !is_inside(canonical_root, package)) {
        add_issue(result, PortableWasmPluginCatalogIssueCode::PathOutsideProject, declared_path, "plugin package path escapes the project root");
        continue;
      }
      if (is_duplicate(package, packages)) {
        add_issue(result, PortableWasmPluginCatalogIssueCode::DuplicatePackage, declared_path,
                  "plugin package resolves to a path already in the catalog");
        continue;
      }
      packages.push_back(package);
    }
    if (!result.issues.empty()) {
      return result;
    }
    std::ranges::sort(packages, [](const auto& left, const auto& right) { return left.generic_string() < right.generic_string(); });

    std::vector<LoadedPortableWasmPlugin> plugins;
    plugins.reserve(packages.size());
    for (const auto& package : packages) {
      auto loaded = load_portable_wasm_plugin_package(package, backend, host_services);
      if (!loaded.plugin.has_value()) {
        add_issue(result, PortableWasmPluginCatalogIssueCode::LoadFailed, package, "plugin package could not be loaded", {},
                  std::move(loaded.issues));
        return result;
      }
      plugins.push_back(std::move(*loaded.plugin));
    }

    modules::CapabilityRegistryBuilder registry_builder;
    for (const auto& provider : builtin_providers) {
      registry_builder.add(provider);
    }
    for (const auto& plugin : plugins) {
      registry_builder.add(plugin.provider());
    }
    auto registry = registry_builder.build();
    if (!registry.ok()) {
      add_issue(result, PortableWasmPluginCatalogIssueCode::RegistryFailed, {}, "plugin and built-in providers do not form a valid registry", {}, {},
                std::move(registry.issues));
      return result;
    }

    result.catalog = std::unique_ptr<PortableWasmPluginCatalog>(new PortableWasmPluginCatalog(std::move(plugins), std::move(*registry.registry)));
    return result;
  }

}  // namespace mobagen::plugins
