#pragma once

#include "asset_cache.hpp"
#include "asset_id.hpp"
#include "resource/resource_handle.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mobagen::assets {

  enum class AssetPipelineStatus : std::uint8_t {
    success,
    invalid_contract,
    import_failed,
    process_failed,
    cook_failed,
    invalid_output,
    cache_error,
    load_failed,
  };

  enum class AssetArtifactKind : std::uint8_t { source, imported, processed, cooked };

  struct AssetStageOutput {
    std::vector<std::byte> bytes;
    std::vector<AssetId> dependencies;
  };

  struct AssetImportRequest {
    std::string_view source_uri;
    AssetId source_id;
    std::span<const std::byte> bytes;
  };

  struct AssetProcessRequest {
    AssetId input_id;
    std::span<const std::byte> bytes;
  };

  struct AssetCookRequest {
    AssetId input_id;
    std::span<const std::byte> bytes;
    std::string_view target;
  };

  struct AssetLoadRequest {
    AssetId id;
    std::span<const std::byte> bytes;
  };

  using AssetImportFunction = bool (*)(void*, const AssetImportRequest&, AssetStageOutput&);
  using AssetProcessFunction = bool (*)(void*, const AssetProcessRequest&, AssetStageOutput&);
  using AssetCookFunction = bool (*)(void*, const AssetCookRequest&, AssetStageOutput&);
  using AssetLoadFunction = bool (*)(void*, const AssetLoadRequest&, resource::Handle&);

  struct AssetImporter {
    std::string_view provider_id;
    void* state{nullptr};
    AssetImportFunction import_asset{nullptr};
  };

  struct AssetProcessor {
    std::string_view provider_id;
    void* state{nullptr};
    AssetProcessFunction process_asset{nullptr};
  };

  struct AssetCooker {
    std::string_view provider_id;
    void* state{nullptr};
    AssetCookFunction cook_asset{nullptr};
  };

  struct AssetLoader {
    std::string_view provider_id;
    void* state{nullptr};
    AssetLoadFunction load_asset{nullptr};
  };

  struct AssetBuildRequest {
    std::string_view source_uri;
    std::span<const std::byte> bytes;
    std::string_view target;
  };

  struct AssetArtifact {
    AssetArtifactKind kind{AssetArtifactKind::source};
    AssetId id;
    std::vector<AssetId> dependencies;
    std::string provider_id;
  };

  struct AssetPipelineResult {
    AssetPipelineStatus status{AssetPipelineStatus::success};
    std::vector<AssetArtifact> artifacts;
  };

  struct AssetLoadResult {
    AssetPipelineStatus status{AssetPipelineStatus::load_failed};
    resource::Handle handle{resource::kNullHandle};
  };

  class AssetPipeline {
  public:
    explicit AssetPipeline(const AssetCache& cache) : cache_(cache) {}

    [[nodiscard]] AssetPipelineResult build(const AssetBuildRequest& request, const AssetImporter& importer,
                                            std::span<const AssetProcessor> processors, const AssetCooker& cooker) const;
    [[nodiscard]] AssetLoadResult load(const AssetId& id, const AssetLoader& loader) const;

  private:
    const AssetCache& cache_;
  };

}  // namespace mobagen::assets
