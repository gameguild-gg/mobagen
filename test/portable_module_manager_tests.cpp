#include <doctest/doctest.h>

#include "modules/locked_activation_plan.hpp"
#include "portable/module_manager.hpp"
#include "plugins/wasm_plugin_loader.hpp"
#include "support/wasm_plugin_test_support.hpp"

#include <mobagen/plugin/wasm_abi.h>

#include <filesystem>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

  std::filesystem::path add_portable_plugin(
      const mobagen::test::TemporaryWasmDirectory& directory, std::string_view name
  ) {
    const auto package = directory.path() / name;
    REQUIRE(std::filesystem::create_directory(package));
    mobagen::test::write_binary(
        package / mobagen::plugins::portable_wasm_plugin_binary_filename(),
        mobagen::test::valid_wasm_header
    );
    return package;
  }

  std::unique_ptr<mobagen::modules::LockedPluginActivationPlan> portable_plan(
      const std::filesystem::path& package, bool staged = false
  ) {
    using namespace mobagen::modules;
    LockfileDocument document;
    document.metadata.plugins = {{
        .provider = "mobagen.wasm-package",
        .version = {1, 0, 0},
        .abi_version = MOBAGEN_WASM_PLUGIN_ABI_VERSION,
        .package = "reference.plugin",
    }};
    document.resolved = {{
        .capability = "runtime.package.v1",
        .provider = "mobagen.wasm-package",
        .version = {1, 0, 0},
        .linkage = LinkageMode::Wasm,
    }};
    const auto binary = package / mobagen::plugins::portable_wasm_plugin_binary_filename();
    if (staged) {
      const std::vector plugins{StagedLockedPlugin{
          .provider_id = "mobagen.wasm-package",
          .version = {1, 0, 0},
          .linkage = LinkageMode::Wasm,
          .abi_version = MOBAGEN_WASM_PLUGIN_ABI_VERSION,
          .expected_hash = mobagen::test::valid_wasm_hash(),
          .package_path = package,
          .binary_path = binary,
      }};
      auto planned = build_locked_plugin_activation_plan(document, plugins);
      REQUIRE(planned.ok());
      return std::move(planned.plan);
    }
    const std::vector plugins{VerifiedLockedPlugin{
        .provider_id = "mobagen.wasm-package",
        .version = {1, 0, 0},
        .linkage = LinkageMode::Wasm,
        .abi_version = MOBAGEN_WASM_PLUGIN_ABI_VERSION,
        .size = mobagen::test::valid_wasm_header.size(),
        .package_path = package,
        .binary_path = binary,
    }};
    auto planned = build_locked_plugin_activation_plan(document, plugins);
    REQUIRE(planned.ok());
    return std::move(planned.plan);
  }

  std::unique_ptr<mobagen::modules::LockedPluginActivationPlan> portable_and_unused_plan(
      const std::filesystem::path& package
  ) {
    using namespace mobagen::modules;
    LockfileDocument document;
    document.metadata.plugins = {
        {.provider = "mobagen.wasm-package",
         .version = {1, 0, 0},
         .abi_version = MOBAGEN_WASM_PLUGIN_ABI_VERSION,
         .package = "reference.plugin"},
        {.provider = "mobagen.wasm-unused",
         .version = {1, 0, 0},
         .abi_version = MOBAGEN_WASM_PLUGIN_ABI_VERSION,
         .package = "unused.plugin"},
    };
    document.resolved = {
        {.capability = "runtime.package.v1",
         .provider = "mobagen.wasm-package",
         .version = {1, 0, 0},
         .linkage = LinkageMode::Wasm},
        {.capability = "runtime.unused.v1",
         .provider = "mobagen.wasm-unused",
         .version = {1, 0, 0},
         .linkage = LinkageMode::Wasm},
    };
    const auto missing = std::filesystem::path{"missing/unused.plugin"};
    const std::vector plugins{
        VerifiedLockedPlugin{
            .provider_id = "mobagen.wasm-package",
            .version = {1, 0, 0},
            .linkage = LinkageMode::Wasm,
            .abi_version = MOBAGEN_WASM_PLUGIN_ABI_VERSION,
            .size = mobagen::test::valid_wasm_header.size(),
            .package_path = package,
            .binary_path = package / mobagen::plugins::portable_wasm_plugin_binary_filename(),
        },
        VerifiedLockedPlugin{
            .provider_id = "mobagen.wasm-unused",
            .version = {1, 0, 0},
            .linkage = LinkageMode::Wasm,
            .abi_version = MOBAGEN_WASM_PLUGIN_ABI_VERSION,
            .size = mobagen::test::valid_wasm_header.size(),
            .package_path = missing,
            .binary_path = missing / mobagen::plugins::portable_wasm_plugin_binary_filename(),
        },
    };
    auto planned = build_locked_plugin_activation_plan(document, plugins);
    REQUIRE(planned.ok());
    return std::move(planned.plan);
  }

  std::unique_ptr<mobagen::modules::LockedPluginActivationPlan> portable_dependency_plan(
      const std::filesystem::path& base_package,
      const std::filesystem::path& app_package
  ) {
    using namespace mobagen::modules;
    LockfileDocument document;
    document.metadata.plugins = {
        {.provider = "mobagen.wasm-base",
         .version = {1, 0, 0},
         .abi_version = MOBAGEN_WASM_PLUGIN_ABI_VERSION,
         .package = "base.plugin"},
        {.provider = "mobagen.wasm-app",
         .version = {1, 0, 0},
         .abi_version = MOBAGEN_WASM_PLUGIN_ABI_VERSION,
         .package = "app.plugin"},
    };
    document.resolved = {
        {.capability = "runtime.base.v1",
         .provider = "mobagen.wasm-base",
         .version = {1, 0, 0},
         .linkage = LinkageMode::Wasm},
        {.capability = "runtime.app.v1",
         .provider = "mobagen.wasm-app",
         .version = {1, 0, 0},
         .linkage = LinkageMode::Wasm},
    };
    document.dependencies = {{
        .capability = "runtime.base.v1",
        .provider = "mobagen.wasm-base",
        .required_by = "mobagen.wasm-app",
    }};
    const std::vector plugins{
        VerifiedLockedPlugin{
            .provider_id = "mobagen.wasm-base",
            .version = {1, 0, 0},
            .linkage = LinkageMode::Wasm,
            .abi_version = MOBAGEN_WASM_PLUGIN_ABI_VERSION,
            .size = mobagen::test::valid_wasm_header.size(),
            .package_path = base_package,
            .binary_path = base_package
                           / mobagen::plugins::portable_wasm_plugin_binary_filename(),
        },
        VerifiedLockedPlugin{
            .provider_id = "mobagen.wasm-app",
            .version = {1, 0, 0},
            .linkage = LinkageMode::Wasm,
            .abi_version = MOBAGEN_WASM_PLUGIN_ABI_VERSION,
            .size = mobagen::test::valid_wasm_header.size(),
            .package_path = app_package,
            .binary_path = app_package
                           / mobagen::plugins::portable_wasm_plugin_binary_filename(),
        },
    };
    auto planned = build_locked_plugin_activation_plan(document, plugins);
    REQUIRE(planned.ok());
    return std::move(planned.plan);
  }

}  // namespace

TEST_CASE("Portable module manager: construction does not instantiate WASM") {
  using namespace mobagen;
  test::FakeWasmBackend backend;
  const auto missing = std::filesystem::path{"missing/reference.plugin"};

  auto created = compositions::create_portable_module_manager(
      portable_plan(missing), backend
  );

  REQUIRE(created.ok());
  CHECK(created.manager->active_count() == 0);
  CHECK(backend.calls == 0);

  const auto activated = created.manager->activate("runtime.package.v1");

  CHECK_FALSE(activated.ok());
  REQUIRE(activated.issues.size() == 1);
  CHECK(activated.issues.front().code
        == compositions::PortableModuleManagerIssueCode::LoadFailed);
  CHECK(backend.calls == 0);
}

TEST_CASE("Portable module manager: first capability request instantiates and activates once") {
  using namespace mobagen;
  test::TemporaryWasmDirectory directory;
  test::FakeWasmBackend backend;
  const auto package = add_portable_plugin(directory, "reference.plugin");
  auto created = compositions::create_portable_module_manager(portable_plan(package), backend);
  REQUIRE(created.ok());

  REQUIRE(created.manager->activate("runtime.package.v1").ok());
  CHECK(created.manager->active_count() == 1);
  CHECK(backend.calls == 1);
  REQUIRE(created.manager->plugin("mobagen.wasm-package") != nullptr);
  CHECK(created.manager->plugin("mobagen.wasm-package")->state()
        == plugins::PortableWasmPluginState::Active);

  REQUIRE(created.manager->activate("runtime.package.v1").ok());
  CHECK(created.manager->active_count() == 1);
  CHECK(backend.calls == 1);
  CHECK(created.manager->stop().ok());
}

TEST_CASE("Portable module manager: capability acquisition returns its active WASM endpoint") {
  using namespace mobagen;
  test::TemporaryWasmDirectory directory;
  test::FakeWasmBackend backend;
  const auto package = add_portable_plugin(directory, "reference.plugin");
  auto created = compositions::create_portable_module_manager(
      portable_plan(package), backend
  );
  REQUIRE(created.ok());

  const auto acquired = created.manager->acquire("runtime.package.v1");

  REQUIRE(acquired.ok());
  REQUIRE(acquired.plugin != nullptr);
  CHECK(acquired.plugin->provider().id == "mobagen.wasm-package");
  CHECK(acquired.plugin->state() == plugins::PortableWasmPluginState::Active);
  CHECK(created.manager->active_count() == 1);
  CHECK(backend.calls == 1);

  const auto reused = created.manager->acquire("runtime.package.v1");
  REQUIRE(reused.ok());
  CHECK(reused.plugin == acquired.plugin);
  CHECK(created.manager->active_count() == 1);
  CHECK(backend.calls == 1);
  CHECK(created.manager->stop().ok());
}

TEST_CASE("Portable module manager: unrelated selected WASM remains uninstantiated") {
  using namespace mobagen;
  test::TemporaryWasmDirectory directory;
  test::FakeWasmBackend backend;
  const auto package = add_portable_plugin(directory, "reference.plugin");
  auto created = compositions::create_portable_module_manager(
      portable_and_unused_plan(package), backend
  );
  REQUIRE(created.ok());

  REQUIRE(created.manager->activate("runtime.package.v1").ok());

  CHECK(created.manager->active_count() == 1);
  CHECK(backend.calls == 1);
  CHECK(created.manager->plugin("mobagen.wasm-unused") == nullptr);
  CHECK(created.manager->stop().ok());
}

TEST_CASE("Portable module manager: requested capability activates its dependency closure") {
  using namespace mobagen;
  test::TemporaryWasmDirectory directory;
  test::FakeWasmBackend backend;
  const auto base = add_portable_plugin(directory, "base.plugin");
  const auto app = add_portable_plugin(directory, "app.plugin");
  backend.provider_ids = {"mobagen.wasm-base", "mobagen.wasm-app"};
  backend.capability_ids = {"runtime.base.v1", "runtime.app.v1"};
  auto created = compositions::create_portable_module_manager(
      portable_dependency_plan(base, app), backend
  );
  REQUIRE(created.ok());

  REQUIRE(created.manager->activate("runtime.app.v1").ok());

  CHECK(created.manager->active_count() == 2);
  CHECK(backend.calls == 2);
  CHECK(created.manager->plugin("mobagen.wasm-base") != nullptr);
  CHECK(created.manager->plugin("mobagen.wasm-app") != nullptr);
  CHECK(created.manager->stop().ok());
}

TEST_CASE("Portable module manager: locked hash is checked before WASM instantiation") {
  using namespace mobagen;
  test::TemporaryWasmDirectory directory;
  test::FakeWasmBackend backend;
  const auto package = add_portable_plugin(directory, "reference.plugin");
  auto plan = portable_plan(package, true);
  auto changed = test::valid_wasm_header;
  changed.back() = std::byte{0x01};
  test::write_binary(package / plugins::portable_wasm_plugin_binary_filename(), changed);
  auto created = compositions::create_portable_module_manager(std::move(plan), backend);
  REQUIRE(created.ok());

  const auto activated = created.manager->activate("runtime.package.v1");

  CHECK_FALSE(activated.ok());
  REQUIRE(activated.issues.size() == 1);
  CHECK(activated.issues.front().code
        == compositions::PortableModuleManagerIssueCode::ArtifactVerificationFailed);
  CHECK(backend.calls == 0);
}

TEST_CASE("Portable module manager: permissions absent from the lock fail before lifecycle") {
  using namespace mobagen;
  test::TemporaryWasmDirectory directory;
  test::FakeWasmBackend backend;
  backend.permission_ids = {"gpu"};
  const auto package = add_portable_plugin(directory, "reference.plugin");
  auto created = compositions::create_portable_module_manager(portable_plan(package), backend);
  REQUIRE(created.ok());

  const auto activated = created.manager->activate("runtime.package.v1");

  CHECK_FALSE(activated.ok());
  REQUIRE(activated.issues.size() == 1);
  CHECK(activated.issues.front().code
        == compositions::PortableModuleManagerIssueCode::PermissionDenied);
  CHECK(backend.calls == 1);
  CHECK(test::invocation_count(backend, plugins::WasmPluginExport::Configure) == 0);
  CHECK(test::invocation_count(backend, plugins::WasmPluginExport::Start) == 0);
}

TEST_CASE("Portable module manager: another thread cannot instantiate WASM") {
  using namespace mobagen;
  test::TemporaryWasmDirectory directory;
  test::FakeWasmBackend backend;
  const auto package = add_portable_plugin(directory, "reference.plugin");
  auto created = compositions::create_portable_module_manager(portable_plan(package), backend);
  REQUIRE(created.ok());
  compositions::PortableModuleManagerActionResult activated;

  std::thread worker{[&] { activated = created.manager->activate("runtime.package.v1"); }};
  worker.join();

  CHECK_FALSE(activated.ok());
  REQUIRE(activated.issues.size() == 1);
  CHECK(activated.issues.front().code
        == compositions::PortableModuleManagerIssueCode::WrongThread);
  CHECK(created.manager->active_count() == 0);
  CHECK(backend.calls == 0);
}
