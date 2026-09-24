#include <doctest/doctest.h>

#include "native/project_runtime.hpp"

#include <mobagen/plugin/runtime_tick_v1.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/asset_id.hpp"

namespace {

  class TemporaryNativeProject {
  public:
    TemporaryNativeProject() {
      const auto root = std::filesystem::temp_directory_path() / "mobagen-native-project-tests";
      std::error_code error;
      std::filesystem::remove_all(root, error);
      REQUIRE(std::filesystem::create_directories(root / "plugins" / "reference.plugin"));
      REQUIRE(std::filesystem::create_directories(root / "plugins" / "unselected.plugin"));
      std::error_code copy_error;
      std::filesystem::copy_file(MOBAGEN_REFERENCE_PLUGIN_PATH, root / "plugins" / "reference.plugin" / mobagen::plugins::native_plugin_binary_filename(),
                                 copy_error);
      REQUIRE_FALSE(copy_error);
      path_ = root;
    }

    ~TemporaryNativeProject() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    void write(std::string_view contents) {
      std::ofstream manifest(path_ / "mobagen.yaml", std::ios::binary | std::ios::trunc);
      REQUIRE(manifest.is_open());
      manifest << contents;
      REQUIRE(manifest.good());
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

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

}  // namespace

/* Lockfile schema v2 (todo 23) removes the dynamic/process linkage vocabulary.
   The native tier (todo 24) is no longer reachable through mobagen.yaml: a
   dynamic profile fails manifest parsing before any loader runs. Loader-level
   native coverage stays in native_module_manager_tests (programmatic lock
   documents); project-level coverage lives in the portable twins. */

TEST_CASE("Native project: dynamic linkage profiles are rejected by schema 2 before the loader runs") {
  using namespace mobagen::compositions;
  TemporaryNativeProject project;
  project.write(R"yaml(schema: 2
name: dynamic-native-project
modules:
  runtime:
    capability: runtime.tick.v1
    use: default
    config:
      schema: mobagen.reference.config.v1
      data: "41"
plugins:
  - ./plugins/reference.plugin
profiles:
  release:
    linkage: dynamic
    editor: false
    permissions:
      - debug
)yaml");

  const auto loaded = load_native_project(project.path() / "mobagen.yaml", runtime_options());

  CHECK_FALSE(loaded.ok());
  REQUIRE(loaded.issues.size() == 1);
  CHECK(loaded.issues.front().code == NativeProjectIssueCode::ParseManifest);
  REQUIRE_FALSE(loaded.issues.front().manifest_errors.empty());
  CHECK(loaded.issues.front().manifest_errors.front().message.find("not supported by project schema version 2") != std::string::npos);
}

TEST_CASE("Native project: process linkage profiles are rejected by schema 2 before the loader runs") {
  using namespace mobagen::compositions;
  TemporaryNativeProject project;
  project.write(R"yaml(schema: 2
name: process-native-project
modules:
  runtime:
    capability: runtime.tick.v1
    use: default
plugins:
  - ./plugins/reference.plugin
profiles:
  release:
    linkage: process
    editor: false
)yaml");

  const auto loaded = load_native_project(project.path() / "mobagen.yaml", runtime_options());

  CHECK_FALSE(loaded.ok());
  REQUIRE(loaded.issues.size() == 1);
  CHECK(loaded.issues.front().code == NativeProjectIssueCode::ParseManifest);
  REQUIRE_FALSE(loaded.issues.front().manifest_errors.empty());
  CHECK(loaded.issues.front().manifest_errors.front().message.find("not supported by project schema version 2") != std::string::npos);
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
