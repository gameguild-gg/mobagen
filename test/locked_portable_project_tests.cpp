#include <doctest/doctest.h>

#include "portable/locked_project.hpp"
#include "portable/project_runtime.hpp"
#include "plugins/wasm_plugin_loader.hpp"
#include "support/wasm_plugin_test_support.hpp"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

namespace {

  class LockedPortableProjectFixture {
  public:
    explicit LockedPortableProjectFixture(bool grants_gpu = false) {
      REQUIRE(std::filesystem::create_directory(directory_.path() / "plugins"));
      package_ = directory_.path() / "plugins/reference.plugin";
      REQUIRE(std::filesystem::create_directory(package_));
      mobagen::test::write_binary(
          binary(), mobagen::test::valid_wasm_header
      );
      const auto permissions = grants_gpu ? "    permissions:\n      - gpu\n" : "";
      mobagen::test::write_text(
          manifest(),
          std::string{"schema: 1\n"
                      "name: locked-portable-project\n"
                      "modules:\n"
                      "  runtime:\n"
                      "    use: mobagen.wasm-package\n"
                      "    capability: runtime.package.v1\n"
                      "plugins:\n"
                      "  - ./plugins/reference.plugin\n"
                      "profiles:\n"
                      "  release:\n"
                      "    linkage: wasm\n"
                      "    editor: false\n"}
              + permissions
      );
    }

    void generate_lock(mobagen::test::FakeWasmBackend& backend) const {
      using namespace mobagen;
      const modules::ResolverOptions options{
          .target = test::portable_target(),
          .profile = "release",
      };
      const compositions::PortableProjectLockOptions lock_options{
          .policy = compositions::PortableProjectLockPolicy::Update,
          .sdk_version = {0, 0, 1},
      };
      auto generated = compositions::load_portable_project(
          manifest(), options, backend, {}, lock_options
      );
      REQUIRE(generated.ok());
      REQUIRE(generated.runtime->stop().ok());
      generated.runtime.reset();
    }

    [[nodiscard]] std::filesystem::path manifest() const {
      return directory_.path() / "mobagen.yaml";
    }
    [[nodiscard]] std::filesystem::path binary() const {
      return package_ / mobagen::plugins::portable_wasm_plugin_binary_filename();
    }

  private:
    mobagen::test::TemporaryWasmDirectory directory_;
    std::filesystem::path package_;
  };

  mobagen::compositions::LockedPortableProjectOptions locked_options() {
    return {
        .sdk_version = {0, 0, 1},
        .target = mobagen::test::portable_target(),
        .profile = "release",
    };
  }

}  // namespace

TEST_CASE("Locked portable project: offline open performs zero WASM instantiations") {
  using namespace mobagen;
  LockedPortableProjectFixture project;
  test::FakeWasmBackend generator;
  project.generate_lock(generator);
  test::FakeWasmBackend backend;

  auto opened = compositions::open_locked_portable_project(
      project.manifest(), locked_options(), backend
  );

  REQUIRE(opened.ok());
  CHECK(opened.product->name == "locked-portable-project");
  CHECK(opened.manager->active_count() == 0);
  CHECK(backend.calls == 0);

  REQUIRE(opened.manager->activate("runtime.package.v1").ok());
  CHECK(opened.manager->active_count() == 1);
  CHECK(backend.calls == 1);
  CHECK(opened.manager->stop().ok());
}

TEST_CASE("Locked portable project: tampered bytes fail only when requested") {
  using namespace mobagen;
  LockedPortableProjectFixture project;
  test::FakeWasmBackend generator;
  project.generate_lock(generator);
  auto changed = test::valid_wasm_header;
  changed.back() = std::byte{0x01};
  test::write_binary(project.binary(), changed);
  test::FakeWasmBackend backend;

  auto opened = compositions::open_locked_portable_project(
      project.manifest(), locked_options(), backend
  );

  REQUIRE(opened.ok());
  CHECK(backend.calls == 0);
  const auto activated = opened.manager->activate("runtime.package.v1");
  CHECK_FALSE(activated.ok());
  REQUIRE(activated.issues.size() == 1);
  CHECK(activated.issues.front().code
        == compositions::PortableModuleManagerIssueCode::ArtifactVerificationFailed);
  CHECK(opened.manager->active_count() == 0);
  CHECK(backend.calls == 0);
}

TEST_CASE("Locked portable project: locked permissions reach the lazy activation") {
  using namespace mobagen;
  LockedPortableProjectFixture project{true};
  test::FakeWasmBackend generator;
  generator.permission_ids = {"gpu"};
  project.generate_lock(generator);
  test::FakeWasmBackend backend;
  backend.permission_ids = {"gpu"};

  auto opened = compositions::open_locked_portable_project(
      project.manifest(), locked_options(), backend
  );

  REQUIRE(opened.ok());
  CHECK(backend.calls == 0);
  REQUIRE(opened.manager->activate("runtime.package.v1").ok());
  REQUIRE(backend.host_imports.size() == 1);
  const auto permissions = backend.host_imports.front()->permissions();
  REQUIRE(permissions.size() == 1);
  CHECK(permissions.front() == "gpu");
  CHECK(opened.manager->stop().ok());
}

TEST_CASE("Locked portable project: lock cannot escalate manifest permissions") {
  using namespace mobagen;
  LockedPortableProjectFixture project;
  test::FakeWasmBackend generator;
  project.generate_lock(generator);
  const auto lock_path = project.manifest().parent_path() / "mobagen.lock";
  auto lock = test::read_text(lock_path);
  const auto permissions = lock.find("permissions: []\n");
  REQUIRE(permissions != std::string::npos);
  lock.replace(
      permissions, std::string_view{"permissions: []\n"}.size(),
      "permissions:\n  - gpu\n"
  );
  test::write_text(lock_path, lock);
  test::FakeWasmBackend backend;

  const auto opened = compositions::open_locked_portable_project(
      project.manifest(), locked_options(), backend
  );

  CHECK_FALSE(opened.ok());
  CHECK(opened.manager == nullptr);
  REQUIRE(opened.issues.size() == 1);
  CHECK(opened.issues.front().code
        == compositions::LockedPortableProjectIssueCode::ProjectMismatch);
  CHECK(backend.calls == 0);
}
