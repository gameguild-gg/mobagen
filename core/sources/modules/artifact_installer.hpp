#pragma once

#include "artifact_fetcher.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace mobagen::modules {

  enum class ArtifactInstallIssueCode : std::uint8_t {
    InvalidRoot,
    InvalidArtifact,
    UnsupportedLinkage,
    SourceInvalid,
    StageFailed,
    CommitFailed,
    RollbackFailed,
    CleanupFailed,
  };

  struct ArtifactInstallIssue {
    ArtifactInstallIssueCode code{};
    std::string provider_id;
    std::filesystem::path path;
    std::error_code system_error;
    std::string message;
  };

  struct InstalledModuleArtifact {
    std::string provider_id;
    SemanticVersion version;
    assets::AssetId id;
    std::uint64_t size{};
    std::filesystem::path package_path;
    std::filesystem::path binary_path;
    bool installed{};
  };

  struct ArtifactInstallResult {
    std::vector<InstalledModuleArtifact> artifacts;
    std::vector<ArtifactInstallIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  [[nodiscard]] std::filesystem::path module_plugin_binary_filename(LinkageMode linkage);

  /* Materializes verified cache blobs as strict .plugin packages without loading executable code. */
  [[nodiscard]] ArtifactInstallResult materialize_module_plugins(
      std::span<const CachedModuleArtifact> artifacts, const std::filesystem::path& install_root
  );

}  // namespace mobagen::modules
