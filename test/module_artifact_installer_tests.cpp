#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/asset_cache.hpp"
#include "modules/artifact_installer.hpp"
#include "modules/lockfile_verifier.hpp"

namespace {

  class TemporaryModuleInstallRoot {
  public:
    TemporaryModuleInstallRoot() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-module-install-" + std::to_string(ticks) + "-"
                 + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryModuleInstallRoot() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
  };

  std::vector<std::byte> artifact_bytes(std::string_view text) {
    const auto source = std::as_bytes(std::span{text.data(), text.size()});
    return {source.begin(), source.end()};
  }

  mobagen::modules::CachedModuleArtifact cache_artifact(
      const mobagen::assets::AssetCache& cache, std::string provider_id, std::string_view contents,
      mobagen::modules::LinkageMode linkage = mobagen::modules::LinkageMode::Dynamic
  ) {
    const auto bytes = artifact_bytes(contents);
    const auto stored = cache.store(bytes);
    REQUIRE(stored.ok());
    REQUIRE(stored.id.has_value());
    return {
        .provider_id = std::move(provider_id),
        .version = {1, 0, 0},
        .id = *stored.id,
        .size = bytes.size(),
        .linkage = linkage,
        .abi_version = 1,
        .cache_path = stored.path,
        .downloaded = true,
    };
  }

  mobagen::modules::LockfileDocument locked_project(
      const mobagen::modules::CachedModuleArtifact& artifact
  ) {
    return {
        .metadata = {
            .sdk = {1, 0, 0},
            .target = mobagen::modules::TargetPlatform::Windows,
            .profile = "release",
            .manifest_hash = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            .plugins = {{
                .provider = artifact.provider_id,
                .version = artifact.version,
                .abi_version = artifact.abi_version,
                .package = ".mobagen/plugins/" + artifact.provider_id + ".plugin",
                .hash = mobagen::assets::to_string(artifact.id),
            }},
        },
        .resolved = {{
            .capability = "runtime.tick.v1",
            .provider = artifact.provider_id,
            .version = artifact.version,
            .linkage = artifact.linkage,
        }},
    };
  }

  mobagen::modules::LockfileVerificationContext locked_context() {
    return {
        .sdk = {1, 0, 0},
        .target = mobagen::modules::TargetPlatform::Windows,
        .profile = "release",
        .manifest_hash = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    };
  }

}  // namespace

TEST_CASE("Module artifact installer: cached native plugins materialize without executing code") {
  using namespace mobagen;
  TemporaryModuleInstallRoot root;
  assets::AssetCache cache(root.path() / "cache", 1024);
  const auto cached = cache_artifact(cache, "mobagen.runtime.remote", "not-an-executable");

  const auto installed = modules::materialize_module_plugins(std::span{&cached, 1}, root.path() / "plugins");

  REQUIRE(installed.ok());
  REQUIRE(installed.artifacts.size() == 1);
  CHECK(installed.artifacts.front().installed);
  CHECK(installed.artifacts.front().abi_version == 1);
  const auto package = root.path() / "plugins" / "mobagen.runtime.remote.plugin";
  const auto binary = package / modules::module_plugin_binary_filename(modules::LinkageMode::Dynamic);
  CHECK(installed.artifacts.front().package_path == package);
  CHECK(installed.artifacts.front().binary_path == binary);
  CHECK(std::filesystem::is_regular_file(binary));

  const auto repeated = modules::materialize_module_plugins(std::span{&cached, 1}, root.path() / "plugins");
  REQUIRE(repeated.ok());
  REQUIRE(repeated.artifacts.size() == 1);
  CHECK_FALSE(repeated.artifacts.front().installed);

  const auto replacement = cache_artifact(cache, "mobagen.runtime.remote", "updated-not-an-executable");
  const auto updated = modules::materialize_module_plugins(std::span{&replacement, 1}, root.path() / "plugins");
  REQUIRE(updated.ok());
  REQUIRE(updated.artifacts.size() == 1);
  CHECK(updated.artifacts.front().installed);
  const auto active = cache.store_file(binary);
  REQUIRE(active.id.has_value());
  CHECK(*active.id == replacement.id);
  CHECK(modules::module_plugin_binary_filename(modules::LinkageMode::Wasm) == "plugin.wasm");
}

TEST_CASE("Module artifact installer: a failed batch preserves every active package") {
  using namespace mobagen;
  TemporaryModuleInstallRoot root;
  assets::AssetCache cache(root.path() / "cache", 1024);
  const auto original = cache_artifact(cache, "mobagen.runtime.first", "original-plugin");
  REQUIRE(modules::materialize_module_plugins(std::span{&original, 1}, root.path() / "plugins").ok());

  auto replacement = cache_artifact(cache, "mobagen.runtime.first", "replacement-plugin");
  auto missing = cache_artifact(cache, "mobagen.runtime.second", "missing-plugin");
  REQUIRE(std::filesystem::remove(missing.cache_path));
  const std::array batch{replacement, missing};

  const auto rejected = modules::materialize_module_plugins(batch, root.path() / "plugins");

  CHECK_FALSE(rejected.ok());
  CHECK(rejected.artifacts.empty());
  const auto active_binary = root.path() / "plugins" / "mobagen.runtime.first.plugin"
                             / modules::module_plugin_binary_filename(modules::LinkageMode::Dynamic);
  const auto active = cache.store_file(active_binary);
  REQUIRE(active.id.has_value());
  CHECK(*active.id == original.id);
  CHECK(std::ranges::none_of(std::filesystem::directory_iterator{root.path() / "plugins"}, [](const auto& entry) {
    return entry.path().filename().string().contains(".tmp-") || entry.path().filename().string().contains(".bak-");
  }));
}

TEST_CASE("Module artifact installer: static artifacts are not runtime plugin packages") {
  using namespace mobagen;
  TemporaryModuleInstallRoot root;
  assets::AssetCache cache(root.path() / "cache", 1024);
  const auto cached = cache_artifact(cache, "mobagen.runtime.static", "static-library", modules::LinkageMode::Static);

  const auto rejected = modules::materialize_module_plugins(std::span{&cached, 1}, root.path() / "plugins");

  CHECK_FALSE(rejected.ok());
  REQUIRE(rejected.issues.size() == 1);
  CHECK(rejected.issues.front().code == modules::ArtifactInstallIssueCode::UnsupportedLinkage);
  CHECK_FALSE(std::filesystem::exists(root.path() / "plugins"));
}

TEST_CASE("Module artifact installer: incompatible ABI never changes an active package") {
  using namespace mobagen;
  TemporaryModuleInstallRoot root;
  assets::AssetCache cache(root.path() / "cache", 1024);
  auto cached = cache_artifact(cache, "mobagen.runtime.future", "future-plugin");
  cached.abi_version = 2;

  const auto rejected = modules::materialize_module_plugins(std::span{&cached, 1}, root.path() / "plugins");

  CHECK_FALSE(rejected.ok());
  REQUIRE(rejected.issues.size() == 1);
  CHECK(rejected.issues.front().code == modules::ArtifactInstallIssueCode::UnsupportedAbi);
  CHECK_FALSE(std::filesystem::exists(root.path() / "plugins"));
}

TEST_CASE("Module lock verifier: installed packages validate offline without loading code") {
  using namespace mobagen;
  TemporaryModuleInstallRoot root;
  assets::AssetCache cache(root.path() / "cache", 1024);
  const auto cached = cache_artifact(cache, "mobagen.runtime.remote", "not-an-executable");
  REQUIRE(modules::materialize_module_plugins(std::span{&cached, 1},
                                              root.path() / ".mobagen" / "plugins").ok());
  const auto document = locked_project(cached);

  const auto verified = modules::verify_locked_project(document, root.path(), locked_context());

  REQUIRE(verified.ok());
  REQUIRE(verified.plugins.size() == 1);
  CHECK(verified.plugins.front().provider_id == cached.provider_id);
  CHECK(verified.plugins.front().linkage == modules::LinkageMode::Dynamic);
  CHECK(verified.plugins.front().size == std::string_view{"not-an-executable"}.size());
  CHECK(verified.plugins.front().binary_path.filename()
        == modules::module_plugin_binary_filename(modules::LinkageMode::Dynamic));
}

TEST_CASE("Module lock verifier: metadata, resolution, and package tampering fail closed") {
  using namespace mobagen;
  TemporaryModuleInstallRoot root;
  assets::AssetCache cache(root.path() / "cache", 1024);
  const auto cached = cache_artifact(cache, "mobagen.runtime.remote", "locked-plugin");
  REQUIRE(modules::materialize_module_plugins(std::span{&cached, 1},
                                              root.path() / ".mobagen" / "plugins").ok());
  auto document = locked_project(cached);

  auto context = locked_context();
  context.profile = "debug";
  auto rejected = modules::verify_locked_project(document, root.path(), context);
  REQUIRE_FALSE(rejected.ok());
  CHECK(rejected.issues.front().code == modules::LockfileVerificationIssueCode::MetadataMismatch);

  document.resolved.front().version = {2, 0, 0};
  rejected = modules::verify_locked_project(document, root.path(), locked_context());
  REQUIRE_FALSE(rejected.ok());
  CHECK(rejected.issues.front().code == modules::LockfileVerificationIssueCode::InvalidResolution);

  document.resolved.front().version = cached.version;
  const auto binary = root.path() / document.metadata.plugins.front().package
                      / modules::module_plugin_binary_filename(modules::LinkageMode::Dynamic);
  std::ofstream(binary, std::ios::binary | std::ios::app) << "tampered";
  rejected = modules::verify_locked_project(document, root.path(), locked_context());
  REQUIRE_FALSE(rejected.ok());
  CHECK(rejected.issues.front().code == modules::LockfileVerificationIssueCode::HashMismatch);
  CHECK(rejected.plugins.empty());
}
