#include <doctest/doctest.h>

#include "assets/asset_manager.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace module_allocation_probe {
  extern std::atomic_bool enabled;
  extern std::atomic_size_t count;
}  // namespace module_allocation_probe

namespace {

  std::span<const std::byte> bytes(std::string_view value) {
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
  }

  class TemporaryAssetManagerDirectory {
  public:
    TemporaryAssetManagerDirectory() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now()
                             .time_since_epoch()
                             .count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-asset-manager-" + std::to_string(ticks) + '-'
                 + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryAssetManagerDirectory() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
      return path_;
    }

  private:
    std::filesystem::path path_;
  };

  struct TextDecoder {
    std::size_t calls{};
    bool fail{};

    static bool decode(
        void* context, const mobagen::assets::AssetDecodeRequest& request,
        std::string& output
    ) {
      auto& decoder = *static_cast<TextDecoder*>(context);
      ++decoder.calls;
      if (decoder.fail) return false;
      output.assign(
          reinterpret_cast<const char*>(request.bytes.data()), request.bytes.size()
      );
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
  AssetManager<std::string> manager{
      cache, {.context = &decoder, .decode = TextDecoder::decode}
  };

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

  bool same_asset = true;
  module_allocation_probe::count.store(0, std::memory_order_relaxed);
  module_allocation_probe::enabled.store(true, std::memory_order_release);
  for (std::size_t index = 0; index < 1'024; ++index) {
    same_asset = same_asset && manager.get(first.handle) != nullptr
                 && *manager.get(first.handle) == "mesh payload";
  }
  module_allocation_probe::enabled.store(false, std::memory_order_release);
  CHECK(same_asset);
  CHECK(module_allocation_probe::count.load(std::memory_order_relaxed) == 0);

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
  AssetManager<std::string> manager{
      cache, {.context = &decoder, .decode = TextDecoder::decode}
  };
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
