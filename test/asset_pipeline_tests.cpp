#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/asset_pipeline.hpp"

namespace {

  std::span<const std::byte> bytes(std::string_view value) { return {reinterpret_cast<const std::byte*>(value.data()), value.size()}; }

  std::string text(std::span<const std::byte> value) { return {reinterpret_cast<const char*>(value.data()), value.size()}; }

  class TemporaryPipelineDirectory {
  public:
    TemporaryPipelineDirectory() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-asset-pipeline-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryPipelineDirectory() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
  };

  struct PipelineState {
    std::vector<std::string> calls;
    std::string loaded;
  };

  bool import_asset(void* opaque, const mobagen::assets::AssetImportRequest& request, mobagen::assets::AssetStageOutput& output) {
    auto& state = *static_cast<PipelineState*>(opaque);
    state.calls.emplace_back("import:" + std::string{request.source_uri});
    const auto transformed = text(request.bytes) + "-imported";
    output.bytes.assign(bytes(transformed).begin(), bytes(transformed).end());
    return true;
  }

  bool process_asset(void* opaque, const mobagen::assets::AssetProcessRequest& request, mobagen::assets::AssetStageOutput& output) {
    auto& state = *static_cast<PipelineState*>(opaque);
    state.calls.emplace_back("process");
    const auto transformed = text(request.bytes) + "-processed";
    output.bytes.assign(bytes(transformed).begin(), bytes(transformed).end());
    return true;
  }

  bool cook_asset(void* opaque, const mobagen::assets::AssetCookRequest& request, mobagen::assets::AssetStageOutput& output) {
    auto& state = *static_cast<PipelineState*>(opaque);
    state.calls.emplace_back("cook:" + std::string{request.target});
    const auto transformed = text(request.bytes) + "-" + std::string{request.target};
    output.bytes.assign(bytes(transformed).begin(), bytes(transformed).end());
    return true;
  }

  bool load_asset(void* opaque, const mobagen::assets::AssetLoadRequest& request, resource::Handle& handle) {
    auto& state = *static_cast<PipelineState*>(opaque);
    state.calls.emplace_back("load");
    state.loaded = text(request.bytes);
    handle = {7, 3};
    return true;
  }

  bool fail_import(void*, const mobagen::assets::AssetImportRequest&, mobagen::assets::AssetStageOutput&) { return false; }

}  // namespace

TEST_CASE("Asset pipeline: importer processors cooker and loader compose end to end") {
  using namespace mobagen::assets;

  TemporaryPipelineDirectory directory;
  AssetCache cache(directory.path(), 4096);
  AssetPipeline pipeline(cache);
  PipelineState state;
  const AssetImporter importer{"test.importer", &state, import_asset};
  const AssetProcessor processor{"test.processor", &state, process_asset};
  const AssetCooker cooker{"test.cooker", &state, cook_asset};
  const AssetBuildRequest request{"memory://mesh.obj", bytes("source"), "windows-x64"};

  const auto result = pipeline.build(request, importer, std::array{processor}, cooker);
  REQUIRE(result.status == AssetPipelineStatus::success);
  REQUIRE(result.artifacts.size() == 4);
  CHECK(result.artifacts[0].kind == AssetArtifactKind::source);
  CHECK(result.artifacts[1].kind == AssetArtifactKind::imported);
  CHECK(result.artifacts[2].kind == AssetArtifactKind::processed);
  CHECK(result.artifacts[3].kind == AssetArtifactKind::cooked);
  CHECK(result.artifacts[1].dependencies == std::vector{result.artifacts[0].id});
  CHECK(result.artifacts[2].dependencies == std::vector{result.artifacts[1].id});
  CHECK(result.artifacts[3].dependencies == std::vector{result.artifacts[2].id});
  const std::vector<std::string> expected_calls{"import:memory://mesh.obj", "process", "cook:windows-x64"};
  CHECK(state.calls == expected_calls);

  const AssetLoader loader{"test.loader", &state, load_asset};
  const auto loaded = pipeline.load(result.artifacts.back().id, loader);
  REQUIRE(loaded.status == AssetPipelineStatus::success);
  CHECK(loaded.handle == (resource::Handle{7, 3}));
  CHECK(state.loaded == "source-imported-processed-windows-x64");
  CHECK(state.calls.back() == "load");
}

TEST_CASE("Asset pipeline: every contract is validated before source cache mutation") {
  using namespace mobagen::assets;

  TemporaryPipelineDirectory directory;
  AssetCache cache(directory.path(), 4096);
  AssetPipeline pipeline(cache);
  PipelineState state;
  const AssetImporter invalid{"Invalid Provider", &state, import_asset};
  const AssetCooker cooker{"test.cooker", &state, cook_asset};

  const auto result = pipeline.build({"memory://asset", bytes("source"), "windows-x64"}, invalid, {}, cooker);
  CHECK(result.status == AssetPipelineStatus::invalid_contract);
  CHECK(result.artifacts.empty());
  CHECK(state.calls.empty());
  CHECK(std::filesystem::is_empty(directory.path()));
}

TEST_CASE("Asset pipeline: stage failure prevents downstream execution") {
  using namespace mobagen::assets;

  TemporaryPipelineDirectory directory;
  AssetCache cache(directory.path(), 4096);
  AssetPipeline pipeline(cache);
  PipelineState state;
  const AssetImporter importer{"test.importer", &state, fail_import};
  const AssetProcessor processor{"test.processor", &state, process_asset};
  const AssetCooker cooker{"test.cooker", &state, cook_asset};

  const auto result = pipeline.build({"memory://asset", bytes("source"), "windows-x64"}, importer, std::array{processor}, cooker);
  CHECK(result.status == AssetPipelineStatus::import_failed);
  CHECK(result.artifacts.empty());
  CHECK(state.calls.empty());
}

TEST_CASE("Asset pipeline: a stage cannot duplicate its implicit input dependency") {
  using namespace mobagen::assets;

  TemporaryPipelineDirectory directory;
  AssetCache cache(directory.path(), 4096);
  AssetPipeline pipeline(cache);
  PipelineState state;
  const AssetImporter importer{"test.importer", &state, [](void*, const AssetImportRequest& request, AssetStageOutput& output) {
                                 output.bytes.assign(request.bytes.begin(), request.bytes.end());
                                 output.dependencies.push_back(request.source_id);
                                 return true;
                               }};
  const AssetCooker cooker{"test.cooker", &state, cook_asset};

  const auto result = pipeline.build({"memory://asset", bytes("source"), "windows-x64"}, importer, {}, cooker);
  CHECK(result.status == AssetPipelineStatus::invalid_output);
  CHECK(result.artifacts.empty());
}
