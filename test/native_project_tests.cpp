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

    void write_lockfile(std::string_view contents) const {
      std::ofstream stream(path_ / "mobagen.lock", std::ios::binary | std::ios::trunc);
      REQUIRE(stream.is_open());
      stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
      REQUIRE(stream.good());
    }

    [[nodiscard]] std::string read_lockfile() const {
      std::ifstream stream(path_ / "mobagen.lock", std::ios::binary);
      REQUIRE(stream.is_open());
      return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
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

  constexpr std::string_view valid_native_project_manifest = R"yaml(schema: 1
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
    permissions:
      - debug
)yaml";

}  // namespace

TEST_CASE("Native project: mobagen yaml default selects and activates a real dot-plugin end to end") {
  TemporaryNativeProject project;
  project.write(valid_native_project_manifest);

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

TEST_CASE("Native project: denied plugin permissions fail before lifecycle activation") {
  using namespace mobagen::compositions;
  TemporaryNativeProject project;
  project.write(R"yaml(schema: 1
name: denied-plugin-permission
modules:
  runtime:
    use: mobagen.reference
plugins:
  - ./plugins/reference.plugin
profiles:
  release:
    linkage: dynamic
    editor: false
)yaml");

  const auto loaded = load_native_project(project.path() / "mobagen.yaml", runtime_options());

  CHECK_FALSE(loaded.ok());
  REQUIRE(loaded.issues.size() == 1);
  CHECK(loaded.issues.front().code == NativeProjectIssueCode::Resolution);
  REQUIRE(loaded.issues.front().resolution_issues.size() == 1);
  CHECK(loaded.issues.front().resolution_issues.front().code == mobagen::modules::ResolutionIssueCode::PermissionDenied);
  std::error_code removal_error;
  CHECK(std::filesystem::remove(project.path() / "plugins" / "reference.plugin" / mobagen::plugins::native_plugin_binary_filename(), removal_error));
  CHECK_FALSE(removal_error);
}

TEST_CASE("Native project: update writes a canonical lock that frozen mode accepts") {
  using namespace mobagen::compositions;
  TemporaryNativeProject project;
  project.write(valid_native_project_manifest);
  const NativeProjectLockOptions update_lock{
      .policy = NativeProjectLockPolicy::Update,
      .sdk_version = mobagen::modules::SemanticVersion{0, 0, 1},
  };

  auto updated = load_native_project(project.path() / "mobagen.yaml", runtime_options(), {}, update_lock);

  REQUIRE(updated.ok());
  const auto generated = updated.runtime->lockfile({0, 0, 1});
  REQUIRE(generated.ok());
  CHECK(project.read_lockfile() == *generated.contents);
  CHECK(updated.runtime->stop().ok());
  updated.runtime.reset();

  const NativeProjectLockOptions frozen_lock{
      .policy = NativeProjectLockPolicy::Frozen,
      .sdk_version = mobagen::modules::SemanticVersion{0, 0, 1},
  };
  auto frozen = load_native_project(project.path() / "mobagen.yaml", runtime_options(), {}, frozen_lock);
  REQUIRE(frozen.ok());
  CHECK(frozen.runtime->host().size() == 1);
  CHECK(frozen.runtime->stop().ok());
}

TEST_CASE("Native project: frozen mode rejects missing and changed lockfiles") {
  using namespace mobagen::compositions;
  TemporaryNativeProject project;
  project.write(valid_native_project_manifest);
  const NativeProjectLockOptions frozen_lock{
      .policy = NativeProjectLockPolicy::Frozen,
      .sdk_version = mobagen::modules::SemanticVersion{0, 0, 1},
  };

  const auto missing = load_native_project(project.path() / "mobagen.yaml", runtime_options(), {}, frozen_lock);
  CHECK_FALSE(missing.ok());
  REQUIRE(missing.issues.size() == 1);
  CHECK(missing.issues.front().code == NativeProjectIssueCode::LockRead);
  REQUIRE(missing.issues.front().lockfile_read_issue.has_value());
  CHECK(missing.issues.front().lockfile_read_issue->code == mobagen::modules::LockfileReadIssueCode::NotFound);

  project.write_lockfile("schema: 1\n# changed\n");
  const auto changed = load_native_project(project.path() / "mobagen.yaml", runtime_options(), {}, frozen_lock);
  CHECK_FALSE(changed.ok());
  REQUIRE(changed.issues.size() == 1);
  CHECK(changed.issues.front().code == NativeProjectIssueCode::LockMismatch);
}

TEST_CASE("Native project: failed lock update rolls back and unloads activated plugins") {
  using namespace mobagen::compositions;
  TemporaryNativeProject project;
  project.write(valid_native_project_manifest);
  REQUIRE(std::filesystem::create_directory(project.path() / "mobagen.lock"));
  const NativeProjectLockOptions update_lock{
      .policy = NativeProjectLockPolicy::Update,
      .sdk_version = mobagen::modules::SemanticVersion{0, 0, 1},
  };

  const auto failed = load_native_project(project.path() / "mobagen.yaml", runtime_options(), {}, update_lock);

  CHECK_FALSE(failed.ok());
  REQUIRE(failed.issues.size() == 1);
  CHECK(failed.issues.front().code == NativeProjectIssueCode::LockWrite);
  REQUIRE(failed.issues.front().lockfile_write_issue.has_value());
  std::error_code removal_error;
  CHECK(std::filesystem::remove(project.path() / "plugins" / "reference.plugin" / mobagen::plugins::native_plugin_binary_filename(), removal_error));
  CHECK_FALSE(removal_error);
}

TEST_CASE("Native project: lock resolution inspects plugins without activating them") {
  using namespace mobagen::compositions;
  TemporaryNativeProject project;
  project.write(R"yaml(schema: 1
name: resolve-only-project
modules:
  runtime:
    use: mobagen.lifecycle-failure
plugins:
  - ./plugins/reference.plugin
  - ./plugins/unselected.plugin
profiles:
  release:
    linkage: dynamic
    editor: false
)yaml");

  const auto resolved = resolve_native_project_lock(project.path() / "mobagen.yaml", runtime_options(), mobagen::modules::SemanticVersion{0, 0, 1});

  REQUIRE(resolved.ok());
  CHECK(resolved.lockfile_path == std::filesystem::weakly_canonical(project.path()) / "mobagen.lock");
  CHECK(resolved.contents->contains("provider: mobagen.lifecycle-failure"));
  REQUIRE(resolved.preview.has_value());
  CHECK(resolved.preview->product.name == "resolve-only-project");
  CHECK(resolved.preview->registry.provider_count() == 2);
  CHECK(resolved.preview->registry.find_provider("mobagen.reference").has_value());
  REQUIRE(resolved.preview->resolution.selections().size() == 1);
  CHECK(resolved.preview->resolution.selections().front().reason == "explicit provider for module 'runtime'");
  CHECK(resolved.preview->resolution.registry_generation() == resolved.preview->registry.generation());
  CHECK_FALSE(std::filesystem::exists(resolved.lockfile_path));

  const auto activated = load_native_project(project.path() / "mobagen.yaml", runtime_options());
  CHECK_FALSE(activated.ok());
  REQUIRE(activated.issues.size() == 1);
  CHECK(activated.issues.front().code == NativeProjectIssueCode::Activation);
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
