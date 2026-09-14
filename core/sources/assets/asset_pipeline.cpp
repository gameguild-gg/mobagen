#include "asset_pipeline.hpp"

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <utility>

namespace mobagen::assets {
  namespace {

    constexpr std::size_t max_provider_id_size = 128;
    constexpr std::size_t max_target_size = 128;
    constexpr std::size_t max_source_uri_size = 4096;

    [[nodiscard]] bool valid_identifier(std::string_view value) {
      if (value.empty() || value.size() > max_provider_id_size
          || !((value.front() >= 'a' && value.front() <= 'z') || (value.front() >= '0' && value.front() <= '9'))) {
        return false;
      }
      return std::ranges::all_of(value, [](const char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '.' || character == '_'
               || character == '-';
      });
    }

    [[nodiscard]] bool valid_target(std::string_view value) {
      return !value.empty() && value.size() <= max_target_size && std::ranges::all_of(value, [](const char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '.' || character == '_'
               || character == '-';
      });
    }

    [[nodiscard]] bool valid_source_uri(std::string_view value) {
      return !value.empty() && value.size() <= max_source_uri_size && value.find('\0') == std::string_view::npos;
    }

    [[nodiscard]] bool valid_contracts(const AssetBuildRequest& request, const AssetImporter& importer, std::span<const AssetProcessor> processors,
                                       const AssetCooker& cooker) {
      if (!valid_source_uri(request.source_uri) || !valid_target(request.target) || !valid_identifier(importer.provider_id)
          || importer.import_asset == nullptr || !valid_identifier(cooker.provider_id) || cooker.cook_asset == nullptr) {
        return false;
      }
      return std::ranges::all_of(
          processors, [](const AssetProcessor& processor) { return valid_identifier(processor.provider_id) && processor.process_asset != nullptr; });
    }

    [[nodiscard]] std::optional<std::vector<AssetId>> canonical_dependencies(const AssetId& input_id, const AssetId& output_id,
                                                                             std::span<const AssetId> declared) {
      std::unordered_set<AssetId, AssetIdHash> unique;
      unique.reserve(declared.size() + 1);
      std::vector<AssetId> result;
      result.reserve(declared.size() + 1);
      if (input_id != output_id) {
        unique.insert(input_id);
        result.push_back(input_id);
      }
      for (const auto& dependency : declared) {
        if (dependency == output_id || !unique.insert(dependency).second) {
          return std::nullopt;
        }
        result.push_back(dependency);
      }
      std::sort(result.begin(), result.end(), [](const AssetId& lhs, const AssetId& rhs) { return lhs.bytes < rhs.bytes; });
      return result;
    }

    [[nodiscard]] bool cache_output(const AssetCache& cache, const AssetStageOutput& output, const AssetId& input_id, AssetArtifactKind kind,
                                    std::string_view provider_id, AssetArtifact& artifact, AssetPipelineStatus& status) {
      const auto stored = cache.store(output.bytes);
      if (!stored.ok() || !stored.id.has_value()) {
        status = AssetPipelineStatus::cache_error;
        return false;
      }
      auto dependencies = canonical_dependencies(input_id, *stored.id, output.dependencies);
      if (!dependencies.has_value()) {
        status = AssetPipelineStatus::invalid_output;
        return false;
      }
      artifact = {kind, *stored.id, std::move(*dependencies), std::string{provider_id}};
      return true;
    }

  }  // namespace

  AssetPipelineResult AssetPipeline::build(const AssetBuildRequest& request, const AssetImporter& importer,
                                           std::span<const AssetProcessor> processors, const AssetCooker& cooker) const {
    if (!valid_contracts(request, importer, processors, cooker)) {
      return {AssetPipelineStatus::invalid_contract, {}};
    }

    const auto source = cache_.store(request.bytes);
    if (!source.ok() || !source.id.has_value()) {
      return {AssetPipelineStatus::cache_error, {}};
    }

    std::vector<AssetArtifact> artifacts;
    artifacts.reserve(processors.size() + 3);
    artifacts.push_back({AssetArtifactKind::source, *source.id, {}, {}});

    AssetStageOutput output;
    bool imported = false;
    try {
      imported = importer.import_asset(importer.state, {request.source_uri, *source.id, request.bytes}, output);
    } catch (...) {
      imported = false;
    }
    if (!imported) {
      return {AssetPipelineStatus::import_failed, {}};
    }

    AssetPipelineStatus status = AssetPipelineStatus::success;
    AssetArtifact artifact;
    if (!cache_output(cache_, output, *source.id, AssetArtifactKind::imported, importer.provider_id, artifact, status)) {
      return {status, {}};
    }
    artifacts.push_back(std::move(artifact));

    auto current_id = artifacts.back().id;
    auto current_bytes = std::move(output.bytes);
    for (const auto& processor : processors) {
      output = {};
      bool processed = false;
      try {
        processed = processor.process_asset(processor.state, {current_id, current_bytes}, output);
      } catch (...) {
        processed = false;
      }
      if (!processed) {
        return {AssetPipelineStatus::process_failed, {}};
      }
      if (!cache_output(cache_, output, current_id, AssetArtifactKind::processed, processor.provider_id, artifact, status)) {
        return {status, {}};
      }
      artifacts.push_back(std::move(artifact));
      current_id = artifacts.back().id;
      current_bytes = std::move(output.bytes);
    }

    output = {};
    bool cooked = false;
    try {
      cooked = cooker.cook_asset(cooker.state, {current_id, current_bytes, request.target}, output);
    } catch (...) {
      cooked = false;
    }
    if (!cooked) {
      return {AssetPipelineStatus::cook_failed, {}};
    }
    if (!cache_output(cache_, output, current_id, AssetArtifactKind::cooked, cooker.provider_id, artifact, status)) {
      return {status, {}};
    }
    artifacts.push_back(std::move(artifact));
    return {AssetPipelineStatus::success, std::move(artifacts)};
  }

  AssetLoadResult AssetPipeline::load(const AssetId& id, const AssetLoader& loader) const {
    if (!valid_identifier(loader.provider_id) || loader.load_asset == nullptr) {
      return {AssetPipelineStatus::invalid_contract, resource::kNullHandle};
    }
    const auto cached = cache_.load(id);
    if (!cached.ok()) {
      return {AssetPipelineStatus::cache_error, resource::kNullHandle};
    }

    resource::Handle handle = resource::kNullHandle;
    bool loaded = false;
    try {
      loaded = loader.load_asset(loader.state, {id, cached.bytes}, handle);
    } catch (...) {
      loaded = false;
    }
    if (!loaded || handle == resource::kNullHandle) {
      return {AssetPipelineStatus::load_failed, resource::kNullHandle};
    }
    return {AssetPipelineStatus::success, handle};
  }

}  // namespace mobagen::assets
