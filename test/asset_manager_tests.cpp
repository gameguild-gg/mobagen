#include <doctest/doctest.h>

#include "assets/asset_manager.hpp"

#include <atomic>
#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace module_allocation_probe {
  extern std::atomic_bool enabled;
  extern std::atomic_size_t count;
}  // namespace module_allocation_probe

namespace {

  std::span<const std::byte> bytes(std::string_view value) { return {reinterpret_cast<const std::byte*>(value.data()), value.size()}; }

  class TemporaryAssetManagerDirectory {
  public:
    TemporaryAssetManagerDirectory() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_
          = std::filesystem::temp_directory_path() / ("mobagen-asset-manager-" + std::to_string(ticks) + '-' + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryAssetManagerDirectory() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
  };

  struct TextDecoder {
    std::size_t calls{};
    bool fail{};
    std::optional<mobagen::assets::AssetId> rejected_id;

    static bool decode(void* context, const mobagen::assets::AssetDecodeRequest& request, std::string& output) {
      auto& decoder = *static_cast<TextDecoder*>(context);
      ++decoder.calls;
      if (decoder.fail || (decoder.rejected_id.has_value() && decoder.rejected_id == request.id)) {
        return false;
      }
      output.assign(reinterpret_cast<const char*>(request.bytes.data()), request.bytes.size());
      return true;
    }
  };

}  // namespace

TEST_CASE("Asset manager: content is decoded lazily and retained by generational handle") {
  using namespace mobagen::assets;
  TemporaryAssetManagerDirectory directory;
  AssetCache cache{directory.path()};
  const auto stored = cache.store(bytes("mesh payload"));
  REQUIRE(stored.ok());
  REQUIRE(stored.id.has_value());
  TextDecoder decoder;
  AssetManager<std::string> manager{cache, {.context = &decoder, .decode = TextDecoder::decode}};

  const auto first = manager.acquire(*stored.id);

  REQUIRE(first.ok());
  CHECK(first.status == AssetManagerStatus::loaded);
  CHECK(first.cache_status == AssetCacheStatus::loaded);
  CHECK(decoder.calls == 1);
  REQUIRE(manager.get(first.handle) != nullptr);
  CHECK(*manager.get(first.handle) == "mesh payload");

  REQUIRE(std::filesystem::remove(stored.path));
  const auto resident = manager.acquire(*stored.id);
  REQUIRE(resident.ok());
  CHECK(resident.status == AssetManagerStatus::resident);
  CHECK(resident.handle == first.handle);
  CHECK_FALSE(resident.cache_status.has_value());
  CHECK(decoder.calls == 1);

  CHECK(manager.release(resident.handle));
  CHECK(manager.get(first.handle) != nullptr);

  bool same_asset = true;
  module_allocation_probe::count.store(0, std::memory_order_relaxed);
  module_allocation_probe::enabled.store(true, std::memory_order_release);
  for (std::size_t index = 0; index < 1'024; ++index) {
    const auto shared = manager.acquire(*stored.id);
    same_asset = same_asset && shared.ok() && shared.status == AssetManagerStatus::resident && shared.handle == first.handle
                 && manager.get(first.handle) != nullptr && *manager.get(first.handle) == "mesh payload" && manager.release(shared.handle);
  }
  module_allocation_probe::enabled.store(false, std::memory_order_release);
  CHECK(same_asset);
  CHECK(module_allocation_probe::count.load(std::memory_order_relaxed) == 0);
  CHECK(decoder.calls == 1);

  CHECK(manager.release(first.handle));
  CHECK(manager.get(first.handle) == nullptr);
  const auto restored = cache.store(bytes("mesh payload"));
  REQUIRE(restored.ok());
  const auto reloaded = manager.acquire(*stored.id);
  REQUIRE(reloaded.ok());
  CHECK(reloaded.handle.index == first.handle.index);
  CHECK(reloaded.handle.generation != first.handle.generation);
  CHECK(decoder.calls == 2);
}

TEST_CASE("Asset manager: missing and rejected assets never become resident") {
  using namespace mobagen::assets;
  TemporaryAssetManagerDirectory directory;
  AssetCache cache{directory.path()};
  TextDecoder decoder;
  AssetManager<std::string> manager{cache, {.context = &decoder, .decode = TextDecoder::decode}};
  const auto missing_id = sha256(bytes("missing payload"));
  REQUIRE(missing_id.has_value());

  const auto missing = manager.acquire(*missing_id);

  CHECK_FALSE(missing.ok());
  CHECK(missing.status == AssetManagerStatus::not_found);
  CHECK(missing.cache_status == AssetCacheStatus::not_found);
  CHECK(decoder.calls == 0);
  CHECK(manager.size() == 0);

  const auto stored = cache.store(bytes("rejected payload"));
  REQUIRE(stored.ok());

  AssetManager<std::string> without_decoder{cache, {}};
  const auto invalid_decoder = without_decoder.acquire(*stored.id);
  CHECK_FALSE(invalid_decoder.ok());
  CHECK(invalid_decoder.status == AssetManagerStatus::invalid_decoder);
  CHECK_FALSE(invalid_decoder.cache_status.has_value());

  decoder.fail = true;
  const auto rejected = manager.acquire(*stored.id);
  CHECK_FALSE(rejected.ok());
  CHECK(rejected.status == AssetManagerStatus::decode_failed);
  CHECK(rejected.cache_status == AssetCacheStatus::loaded);
  CHECK(decoder.calls == 1);
  CHECK(manager.size() == 0);
}

TEST_CASE("Asset manager: dependency closure loads transactionally in dependency order") {
  using namespace mobagen::assets;
  TemporaryAssetManagerDirectory directory;
  AssetCache cache{directory.path()};
  const auto source = cache.store(bytes("source asset"));
  const auto material = cache.store(bytes("material asset"));
  const auto scene = cache.store(bytes("scene asset"));
  REQUIRE(source.ok());
  REQUIRE(material.ok());
  REQUIRE(scene.ok());

  AssetDependencyGraph graph;
  REQUIRE(graph.register_asset(*source.id));
  REQUIRE(graph.register_asset(*material.id));
  REQUIRE(graph.register_asset(*scene.id));
  REQUIRE(graph.set_dependencies(*material.id, std::array{*source.id}) == AssetDependencyStatus::success);
  REQUIRE(graph.set_dependencies(*scene.id, std::array{*material.id}) == AssetDependencyStatus::success);

  TextDecoder decoder;
  AssetManager<std::string> manager{cache, {.context = &decoder, .decode = TextDecoder::decode}};
  const auto resident_source = manager.acquire(*source.id);
  REQUIRE(resident_source.ok());
  decoder.rejected_id = *scene.id;

  const auto failed = manager.acquire_all(graph, std::array{*scene.id});

  CHECK_FALSE(failed.ok());
  CHECK(failed.status == AssetManagerBatchStatus::asset_error);
  CHECK(failed.dependency_status == AssetDependencyStatus::success);
  CHECK(failed.failed_asset == scene.id);
  REQUIRE(failed.failure.has_value());
  CHECK(failed.failure->status == AssetManagerStatus::decode_failed);
  CHECK(failed.assets.empty());
  CHECK(manager.find(*source.id) == resident_source.handle);
  CHECK_FALSE(manager.find(*material.id).has_value());
  CHECK_FALSE(manager.find(*scene.id).has_value());
  CHECK(manager.size() == 1);
  CHECK(manager.release(resident_source.handle));
  CHECK(manager.size() == 0);

  decoder.rejected_id.reset();
  const auto loaded = manager.acquire_all(graph, std::array{*scene.id});
  const auto expected = graph.build_order(std::array{*scene.id});
  REQUIRE(loaded.ok());
  CHECK(loaded.status == AssetManagerBatchStatus::success);
  CHECK(loaded.dependency_status == AssetDependencyStatus::success);
  CHECK_FALSE(loaded.failed_asset.has_value());
  CHECK_FALSE(loaded.failure.has_value());
  REQUIRE(expected.status == AssetDependencyStatus::success);
  REQUIRE(loaded.assets.size() == expected.assets.size());
  for (std::size_t index = 0; index < expected.assets.size(); ++index) {
    CHECK(loaded.assets[index].id == expected.assets[index]);
    CHECK(manager.valid(loaded.assets[index].handle));
  }
  CHECK(manager.size() == 3);
  for (auto asset = loaded.assets.rbegin(); asset != loaded.assets.rend(); ++asset) {
    CHECK(manager.release(asset->handle));
  }
  CHECK(manager.size() == 0);
}

TEST_CASE("Asset manager: unknown dependency roots fail before loading") {
  using namespace mobagen::assets;
  TemporaryAssetManagerDirectory directory;
  AssetCache cache{directory.path()};
  TextDecoder decoder;
  AssetManager<std::string> manager{cache, {.context = &decoder, .decode = TextDecoder::decode}};
  const auto missing = sha256(bytes("unknown graph root"));
  REQUIRE(missing.has_value());
  AssetDependencyGraph graph;

  const auto failed = manager.acquire_all(graph, std::array{*missing});

  CHECK_FALSE(failed.ok());
  CHECK(failed.status == AssetManagerBatchStatus::dependency_error);
  CHECK(failed.dependency_status == AssetDependencyStatus::unknown_asset);
  CHECK_FALSE(failed.failed_asset.has_value());
  CHECK_FALSE(failed.failure.has_value());
  CHECK(failed.assets.empty());
  CHECK(decoder.calls == 0);
  CHECK(manager.size() == 0);
}
