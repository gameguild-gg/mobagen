#include <doctest/doctest.h>

#include "assets/asset_cache.hpp"
#include "plugins/asset_store_v1.h"
#include "plugins/plugin_activation.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

  std::span<const std::byte> bytes(std::string_view value) {
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
  }

  class TemporaryAssetPluginDirectory {
  public:
    TemporaryAssetPluginDirectory() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now()
                             .time_since_epoch()
                             .count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-asset-plugin-" + std::to_string(ticks) + '-'
                 + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryAssetPluginDirectory() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
      return path_;
    }

  private:
    std::filesystem::path path_;
  };

  MobagenAssetIdV1 abi_id(const mobagen::assets::AssetId& id) {
    MobagenAssetIdV1 result{};
    std::ranges::copy(id.bytes, result.bytes);
    return result;
  }

}  // namespace

TEST_CASE("Asset store plugin: real package publishes the default native provider") {
  using namespace mobagen;
  TemporaryAssetPluginDirectory directory;
  const auto package = directory.path() / "mobagen.assets.default.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  REQUIRE(std::filesystem::copy_file(
      MOBAGEN_DEFAULT_ASSET_STORE_PLUGIN_PATH,
      package / plugins::native_plugin_binary_filename()
  ));

  const auto cache_root = directory.path() / "cache";
  assets::AssetCache cache{cache_root};
  const auto stored = cache.store(bytes("asset from a real plugin"));
  REQUIRE(stored.ok());
  const auto configuration_text = cache_root.string();
  const auto configuration = std::as_bytes(std::span{configuration_text});
  plugins::PluginHost host;

  {
    const auto loaded = plugins::load_native_plugin_package(package, host.api());
    REQUIRE(loaded.plugin.has_value());
    const auto& provider = loaded.plugin->contract().provider;
    CHECK(provider.id == "mobagen.assets.default");
    CHECK(provider.provides
          == std::vector<std::string>{MOBAGEN_ASSET_STORE_V1_ID});
    CHECK(provider.reload == modules::ReloadPolicy::Restart);
    CHECK(provider.configuration_schema == "mobagen.assets.default.config.v1");
    CHECK(provider.permissions == std::vector<std::string>{"filesystem-read"});
  }

  auto activated = plugins::activate_native_plugin_package(
      package, host, configuration
  );

  REQUIRE(activated.ok());
  CHECK(activated.activation->provider_id() == "mobagen.assets.default");
  const auto api = host.find<MobagenAssetStoreV1>(
      MOBAGEN_ASSET_STORE_V1_ID, MOBAGEN_ASSET_STORE_V1_ABI_VERSION
  );
  REQUIRE(api.has_value());
  const auto id = abi_id(*stored.id);
  MobagenAssetHandleV1 handle{};
  REQUIRE((*api)->acquire((*api)->store_state, &id, &handle)
          == MOBAGEN_STATUS_OK);
  MobagenByteView view{};
  REQUIRE((*api)->view((*api)->store_state, handle, &view)
          == MOBAGEN_STATUS_OK);
  CHECK(view.size == bytes("asset from a real plugin").size());
  CHECK(std::equal(
      view.data, view.data + view.size,
      reinterpret_cast<const std::uint8_t*>("asset from a real plugin")
  ));
  CHECK((*api)->release((*api)->store_state, handle) == MOBAGEN_STATUS_OK);

  CHECK(activated.activation->quiesce().ok());
  CHECK(activated.activation->stop().ok());
  CHECK_FALSE(host.find<MobagenAssetStoreV1>(MOBAGEN_ASSET_STORE_V1_ID, 1)
                  .has_value());
}
