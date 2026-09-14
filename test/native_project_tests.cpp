#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/asset_id.hpp"
#include "native/project_runtime.hpp"
#include "plugins/runtime_tick_v1.h"

namespace {

  class TemporaryNativeProject {
  public:
    TemporaryNativeProject() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-native-project-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directories(path_ / "plugins" / "reference.plugin"));
      REQUIRE(std::filesystem::copy_file(MOBAGEN_REFERENCE_PLUGIN_PATH,
                                         path_ / "plugins" / "reference.plugin" / mobagen::plugins::native_plugin_binary_filename()));
      REQUIRE(std::filesystem::create_directories(path_ / "plugins" / "unselected.plugin"));
      REQUIRE(std::filesystem::copy_file(MOBAGEN_CONFIGURE_FAILURE_PLUGIN_PATH,
                                         path_ / "plugins" / "unselected.plugin" / mobagen::plugins::native_plugin_binary_filename()));
    }

    ~TemporaryNativeProject() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    void write(std::string_view contents) const {
      std::ofstream stream(path_ / "mobagen.yaml", std::ios::binary | std::ios::trunc);
      REQUIRE(stream.is_open());
      stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
      REQUIRE(stream.good());
    }

  private:
    std::filesystem::path path_;
  };

  mobagen::modules::TargetPlatform native_target() {
#ifdef _WIN32
    return mobagen::modules::TargetPlatform::Windows;
#elif defined(__APPLE__)
    return mobagen::modules::TargetPlatform::MacOS;
#else
    return mobagen::modules::TargetPlatform::Linux;
#endif
  }

  mobagen::modules::ResolverOptions runtime_options() {
    return {
        .target = native_target(),
        .profile = "release",
        .aliases = {{.alias = "runtime", .capability = MOBAGEN_RUNTIME_TICK_V1_ID}},
        .defaults = {{.target = native_target(), .profile = "release", .capability = MOBAGEN_RUNTIME_TICK_V1_ID, .provider = "mobagen.reference"}},
    };
  }

  std::string reference_plugin_hash() {
    std::ifstream stream(MOBAGEN_REFERENCE_PLUGIN_PATH, std::ios::binary);
    REQUIRE(stream.is_open());
    const std::vector<char> contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    const auto bytes = std::span<const std::byte>{reinterpret_cast<const std::byte*>(contents.data()), contents.size()};
    const auto hash = mobagen::assets::sha256(bytes);
    REQUIRE(hash.has_value());
    return mobagen::assets::to_string(*hash);
  }

  std::string native_target_name() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
  }

}  // namespace

TEST_CASE("Native project: mobagen yaml default selects and activates a real dot-plugin end to end") {
  TemporaryNativeProject project;
  project.write(R"yaml(schema: 1
name: native-project-test
modules:
  runtime:
    use: default
plugins:
  - ./plugins/reference.plugin
  - ./plugins/unselected.plugin
profiles:
  release:
    linkage: dynamic
    editor: false
)yaml");

  auto loaded = mobagen::compositions::load_native_project(project.path() / "mobagen.yaml", runtime_options());

  REQUIRE(loaded.ok());
  CHECK(loaded.runtime->product().name == "native-project-test");
  CHECK(loaded.runtime->resolution().lifecycle_order().size() == 1);
  const auto api = loaded.runtime->host().find<MobagenRuntimeTickV1>(MOBAGEN_RUNTIME_TICK_V1_ID, 1);
  REQUIRE(api.has_value());
  CHECK((*api)->tick((*api)->plugin_state) == MOBAGEN_STATUS_OK);
  CHECK((*api)->tick_count((*api)->plugin_state) == 1);
  const auto lockfile = loaded.runtime->lockfile({0, 0, 1});
  REQUIRE(lockfile.ok());
  CHECK_FALSE(lockfile.contents->contains("mobagen.lifecycle-failure"));
  CHECK(*lockfile.contents
        == "schema: 1\n"
           "sdk: 0.0.1\n"
           "target: "
               + native_target_name()
               + "\n"
                 "profile: release\n"
                 "resolved:\n"
                 "  runtime.tick.v1:\n"
                 "    provider: mobagen.reference\n"
                 "    version: 1.0.0\n"
                 "    linkage: dynamic\n"
                 "dependencies: []\n"
                 "plugins:\n"
                 "  mobagen.reference:\n"
                 "    version: 1.0.0\n"
                 "    abi: 1\n"
                 "    package: \"plugins/reference.plugin\"\n"
                 "    hash: "
               + reference_plugin_hash() + "\n");
  CHECK(loaded.runtime->stop().ok());
  CHECK(loaded.runtime->host().size() == 0);
}

TEST_CASE("Native project: missing oversized and malformed manifests fail before runtime publication") {
  using namespace mobagen::compositions;
  TemporaryNativeProject project;

  const auto missing = load_native_project(project.path() / "missing.yaml", runtime_options());
  CHECK_FALSE(missing.ok());
  REQUIRE_FALSE(missing.issues.empty());
  CHECK(missing.issues.front().code == NativeProjectIssueCode::ReadManifest);

  project.write(std::string(mobagen::modules::max_product_manifest_bytes + 1, 'x'));
  const auto oversized = load_native_project(project.path() / "mobagen.yaml", runtime_options());
  CHECK_FALSE(oversized.ok());
  REQUIRE_FALSE(oversized.issues.empty());
  CHECK(oversized.issues.front().code == NativeProjectIssueCode::ReadManifest);

  project.write("schema: [1\n");
  const auto malformed = load_native_project(project.path() / "mobagen.yaml", runtime_options());
  CHECK_FALSE(malformed.ok());
  REQUIRE_FALSE(malformed.issues.empty());
  CHECK(malformed.issues.front().code == NativeProjectIssueCode::ParseManifest);
}
