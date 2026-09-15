#include <doctest/doctest.h>

#include "catalog_publisher.hpp"
#include "modules/catalog_parser.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

  class TemporaryCatalogDirectory {
  public:
    TemporaryCatalogDirectory() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-catalog-publisher-" + std::to_string(ticks) + '-' + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryCatalogDirectory() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
  };

  std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
  }

  constexpr mobagen::modules::TargetPlatform native_catalog_target() noexcept {
#if defined(_WIN32)
    return mobagen::modules::TargetPlatform::Windows;
#elif defined(__APPLE__)
    return mobagen::modules::TargetPlatform::MacOS;
#else
    return mobagen::modules::TargetPlatform::Linux;
#endif
  }

  constexpr std::string_view native_catalog_target_name() noexcept {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
  }

}  // namespace

TEST_CASE("Module catalog publisher: real plugins produce deterministic distributable metadata") {
  using namespace mobagen;
  TemporaryCatalogDirectory directory;
  const tools::NativeCatalogPublishOptions options{
      .output_root = directory.path() / "published",
      .base_url = "https://plugins.mobagen.dev/v1",
      .target = native_catalog_target(),
      .plugin_binaries = {MOBAGEN_DEFAULT_ASSET_STORE_PLUGIN_PATH},
  };

  const auto first = tools::publish_native_module_catalog(options);

  REQUIRE(first.ok());
  REQUIRE(first.catalog_path.has_value());
  const auto catalog_text = read_text(*first.catalog_path);
  const auto parsed = modules::parse_module_catalog(catalog_text, "catalog.yaml");
  REQUIRE(parsed.ok());
  REQUIRE(parsed.catalog->providers.size() == 1);
  const auto& published = parsed.catalog->providers.front();
  CHECK(published.provider.id == "mobagen.assets.default");
  CHECK(published.provider.provides == std::vector<std::string>{"assets.store.v1"});
  CHECK(published.provider.permissions == std::vector<std::string>{"filesystem-read"});
  REQUIRE(published.artifacts.size() == 1);
  CHECK(published.artifacts.front().abi_version == 1);
  CHECK(published.artifacts.front().url
        == "https://plugins.mobagen.dev/v1/mobagen.assets.default/1.0.0/" + std::string{native_catalog_target_name()} + ".plugin");
  CHECK(published.artifacts.front().hash.starts_with("sha256:"));
  CHECK(published.artifacts.front().size == std::filesystem::file_size(MOBAGEN_DEFAULT_ASSET_STORE_PLUGIN_PATH));
  const auto artifact = directory.path() / "published" / "mobagen.assets.default" / "1.0.0" / (std::string{native_catalog_target_name()} + ".plugin");
  REQUIRE(std::filesystem::is_regular_file(artifact));
  CHECK(std::filesystem::file_size(artifact) == std::filesystem::file_size(MOBAGEN_DEFAULT_ASSET_STORE_PLUGIN_PATH));

  const auto second = tools::publish_native_module_catalog(options);
  REQUIRE(second.ok());
  CHECK(read_text(*second.catalog_path) == catalog_text);
}

TEST_CASE("Module catalog publisher: invalid input preserves the active catalog") {
  using namespace mobagen;
  TemporaryCatalogDirectory directory;
  const auto output = directory.path() / "published";
  const tools::NativeCatalogPublishOptions valid{
      .output_root = output,
      .base_url = "https://plugins.mobagen.dev/v1",
      .target = native_catalog_target(),
      .plugin_binaries = {MOBAGEN_DEFAULT_ASSET_STORE_PLUGIN_PATH},
  };
  REQUIRE(tools::publish_native_module_catalog(valid).ok());
  const auto original = read_text(output / "catalog.yaml");

  auto invalid = valid;
  invalid.plugin_binaries.push_back(directory.path() / "missing.dll");
  const auto rejected = tools::publish_native_module_catalog(invalid);

  CHECK_FALSE(rejected.ok());
  CHECK(read_text(output / "catalog.yaml") == original);
}
