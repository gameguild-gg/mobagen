#include <doctest/doctest.h>

#include "portable/locked_project.hpp"
#include "portable/project_runtime.hpp"
#include "project_module_manager.hpp"
#include "project_startup.hpp"
#include "http/client.hpp"
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

  class PortableCatalogHttpClient final : public mobagen::http::Client {
  public:
    mobagen::http::GetResult get(
        const mobagen::http::GetRequest& request
    ) override {
      static_cast<void>(request);
      ++catalog_requests;
      const auto catalog =
          std::string{"schema: 1\n"
                      "providers:\n"
                      "  mobagen.wasm-package:\n"
                      "    version: 1.0.0\n"
                      "    provides: [runtime.package.v1]\n"
                      "    reload: restart\n"
                      "    artifacts:\n"
                      "      - target: "}
          + mobagen::test::portable_target_name()
          + "\n"
            "        linkage: wasm\n"
            "        abi: 1\n"
            "        url: https://plugins.mobagen.dev/reference.plugin\n"
            "        size: "
          + std::to_string(mobagen::test::valid_wasm_header.size())
          + "\n"
            "        hash: "
          + mobagen::test::valid_wasm_hash() + "\n";
      std::vector<std::byte> body(catalog.size());
      for (std::size_t index = 0; index < catalog.size(); ++index) {
        body[index] = static_cast<std::byte>(catalog[index]);
      }
      return {
          .response = mobagen::http::Response{200, std::move(body)},
      };
    }

    mobagen::http::StreamGetResult get_stream(
        const mobagen::http::GetRequest& request,
        mobagen::http::BodySink sink
    ) override {
      static_cast<void>(request);
      ++artifact_requests;
      if (!sink.write(sink.context, mobagen::test::valid_wasm_header)) {
        return {
            .error = mobagen::http::Error{
                mobagen::http::ErrorCode::SinkRejected,
                "artifact cache rejected test WASM bytes",
            },
        };
      }
      return {
          .response = mobagen::http::StreamResponse{
              200, mobagen::test::valid_wasm_header.size()
          },
      };
    }

    std::size_t catalog_requests{};
    std::size_t artifact_requests{};
  };

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

TEST_CASE("Project module manager: manifest profile routes to lazy portable modules") {
  using namespace mobagen;
  LockedPortableProjectFixture project;
  test::FakeWasmBackend generator;
  project.generate_lock(generator);
  test::FakeWasmBackend backend;

  auto opened = compositions::open_locked_project(
      project.manifest(),
      {.sdk_version = {0, 0, 1},
       .target = test::portable_target(),
       .profile = "release"},
      {.portable_backend = &backend}
  );

  REQUIRE(opened.ok());
  CHECK(opened.product->name == "locked-portable-project");
  CHECK(opened.manager->kind()
        == compositions::ProjectModuleRuntimeKind::Portable);
  CHECK(opened.manager->native() == nullptr);
  CHECK(opened.manager->portable() != nullptr);
  CHECK(opened.manager->active_count() == 0);
  CHECK(backend.calls == 0);

  REQUIRE(opened.manager->activate("runtime.package.v1").ok());
  CHECK(opened.manager->active_count() == 1);
  CHECK(backend.calls == 1);
  CHECK(opened.manager->stop().ok());
}

TEST_CASE("Project startup: first run downloads portable modules without instantiating them") {
  using namespace mobagen;
  test::TemporaryWasmDirectory project;
  test::write_text(
      project.path() / "mobagen.yaml",
      R"yaml(schema: 1
name: portable-first-run
sources:
  official:
    url: https://plugins.mobagen.dev/catalog.yaml
modules:
  runtime:
    capability: runtime.package.v1
    use: mobagen.wasm-package
plugins: []
profiles:
  release:
    linkage: wasm
    editor: false
)yaml"
  );
  PortableCatalogHttpClient client;
  test::FakeWasmBackend backend;

  auto started = compositions::prepare_and_open_project(
      project.path() / "mobagen.yaml",
      {
          .resolver = {
              .target = test::portable_target(),
              .profile = "release",
          },
          .sdk_version = {0, 0, 1},
      },
      {
          .http_client = &client,
          .modules = {.portable_backend = &backend},
      }
  );

  REQUIRE(started.ok());
  CHECK(started.bootstrap.state
        == compositions::ProjectBootstrapState::Synchronized);
  CHECK(started.project.manager->kind()
        == compositions::ProjectModuleRuntimeKind::Portable);
  CHECK(started.project.manager->active_count() == 0);
  CHECK(client.catalog_requests == 1);
  CHECK(client.artifact_requests == 1);
  CHECK(backend.calls == 0);

  REQUIRE(started.project.manager->activate("runtime.package.v1").ok());
  CHECK(started.project.manager->active_count() == 1);
  CHECK(backend.calls == 1);
  CHECK(started.project.manager->stop().ok());
}

TEST_CASE("Project module manager: portable profile requires an injected backend") {
  using namespace mobagen;
  LockedPortableProjectFixture project;
  test::FakeWasmBackend generator;
  project.generate_lock(generator);

  const auto opened = compositions::open_locked_project(
      project.manifest(),
      {.sdk_version = {0, 0, 1},
       .target = test::portable_target(),
       .profile = "release"}
  );

  CHECK_FALSE(opened.ok());
  CHECK(opened.manager == nullptr);
  REQUIRE(opened.issues.size() == 1);
  CHECK(opened.issues.front().code
        == compositions::LockedProjectIssueCode::PortableBackendUnavailable);
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
