#pragma once

#include "modules/catalog.hpp"
#include "plugins/plugin_loader.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mobagen::tools {

  enum class NativeCatalogPublishIssueCode : std::uint8_t {
    InvalidOptions,
    InvalidOutput,
    InvalidPlugin,
    DuplicateProvider,
    ArtifactReadFailed,
    ArtifactConflict,
    ArtifactWriteFailed,
    CatalogWriteFailed,
  };

  struct NativeCatalogPublishIssue {
    NativeCatalogPublishIssueCode code{};
    std::filesystem::path path;
    std::string message;
    std::vector<plugins::NativePluginLoadIssue> plugin_issues;
  };

  struct NativeCatalogPublishOptions {
    std::filesystem::path output_root;
    std::string base_url;
    modules::TargetPlatform target{};
    std::vector<std::filesystem::path> plugin_binaries;
  };

  struct NativeCatalogPublishResult {
    std::optional<std::filesystem::path> catalog_path;
    std::vector<modules::PublishedProviderDescriptor> providers;
    std::vector<NativeCatalogPublishIssue> issues;

    [[nodiscard]] bool ok() const noexcept {
      return catalog_path.has_value() && issues.empty();
    }
  };

  [[nodiscard]] NativeCatalogPublishResult publish_native_module_catalog(
      const NativeCatalogPublishOptions& options
  );

}  // namespace mobagen::tools
