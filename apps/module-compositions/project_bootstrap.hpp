#pragma once

#include "modules/artifact_fetcher.hpp"
#include "modules/artifact_installer.hpp"
#include "modules/catalog.hpp"
#include "modules/resolver.hpp"

#include <mobagen/version.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mobagen::http {
  class Client;
}

namespace mobagen::compositions {

  enum class ProjectBootstrapMode : std::uint8_t { Ensure, Refresh };
  enum class ProjectBootstrapValidation : std::uint8_t { Lazy, Full };
  enum class ProjectBootstrapState : std::uint8_t { Ready, Synchronized };

  struct ProjectBootstrapOptions {
    modules::ResolverOptions resolver;
    modules::SemanticVersion sdk_version{
        MOBAGEN_SDK_VERSION_MAJOR, MOBAGEN_SDK_VERSION_MINOR,
        MOBAGEN_SDK_VERSION_PATCH
    };
    std::optional<std::filesystem::path> cache_root;
    ProjectBootstrapMode mode{ProjectBootstrapMode::Ensure};
    ProjectBootstrapValidation validation{ProjectBootstrapValidation::Lazy};
  };

  enum class ProjectBootstrapIssueCode : std::uint8_t {
    ReadManifest,
    ParseManifest,
    HashManifest,
    ProfileUnavailable,
    HttpUnavailable,
    SyncPlan,
    CachePath,
    ArtifactFetch,
    ArtifactInstall,
    Lockfile,
  };

  struct ProjectBootstrapIssue {
    ProjectBootstrapIssueCode code{};
    std::string message;
  };

  struct ProjectBootstrapSelection {
    std::string provider_id;
    modules::SemanticVersion version;
    modules::ModuleArtifactDescriptor artifact;
  };

  struct ProjectBootstrapResult {
    std::optional<ProjectBootstrapState> state;
    std::string product_name;
    std::string profile;
    std::filesystem::path lockfile_path;
    std::size_t plugin_count{};
    std::size_t selected_count{};
    std::vector<ProjectBootstrapSelection> selections;
    std::vector<modules::CachedModuleArtifact> cached_artifacts;
    std::vector<modules::InstalledModuleArtifact> installed_artifacts;
    std::vector<ProjectBootstrapIssue> issues;

    [[nodiscard]] bool ok() const noexcept {
      return state.has_value() && issues.empty();
    }
  };

  /* Ensures that a project has a deterministic local lock and installed
     .plugin packages. Network access is injected and used only when local
     state is missing, stale, or Refresh is requested. Lazy validation never
     reads plugin bodies; activation verifies the requested dependency closure. */
  [[nodiscard]] ProjectBootstrapResult bootstrap_project(
      const std::filesystem::path& manifest_path, ProjectBootstrapOptions options,
      http::Client* http_client = nullptr
  );

}  // namespace mobagen::compositions
