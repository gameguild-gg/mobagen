#pragma once

#include "assets/asset_cache.hpp"
#include "catalog_index.hpp"
#include "http/client.hpp"
#include "resolver.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mobagen::modules {

  struct ArtifactFetchOptions {
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds transfer_timeout{300000};
  };

  enum class ArtifactFetchIssueCode : std::uint8_t {
    InvalidPlan,
    InvalidArtifact,
    Transport,
    HttpStatus,
    SizeMismatch,
    IntegrityMismatch,
    CacheFailure,
  };

  struct ArtifactFetchIssue {
    ArtifactFetchIssueCode code{};
    std::string provider_id;
    std::string message;
    std::optional<std::uint16_t> http_status;
    std::optional<http::Error> transport_error;
    std::optional<assets::AssetCacheStatus> cache_status;
  };

  struct CachedModuleArtifact {
    std::string provider_id;
    SemanticVersion version;
    assets::AssetId id;
    std::filesystem::path cache_path;
    bool downloaded{};
  };

  struct ArtifactFetchResult {
    std::vector<CachedModuleArtifact> artifacts;
    std::vector<ArtifactFetchIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  [[nodiscard]] ArtifactFetchResult fetch_module_artifacts(
      const ModuleCatalogIndex& catalog, const ModuleResolution& resolution, http::Client& client,
      const assets::AssetCache& cache, ArtifactFetchOptions options = {}
  );

}  // namespace mobagen::modules
