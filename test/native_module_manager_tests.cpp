#include <doctest/doctest.h>

#include "modules/locked_activation_plan.hpp"
#include "native/module_manager.hpp"
#include "assets/asset_id.hpp"
#include "plugins/plugin_loader.hpp"
#include "plugins/runtime_tick_v1.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

  class TemporaryNativeModuleProject {
  public:
    TemporaryNativeModuleProject() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-native-module-manager-" + std::to_string(ticks) + '-'
                 + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directories(path_));
    }

    ~TemporaryNativeModuleProject() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] std::filesystem::path add_reference_plugin() const {
      const auto package = path_ / "reference.plugin";
      REQUIRE(std::filesystem::create_directory(package));
      REQUIRE(std::filesystem::copy_file(
          MOBAGEN_REFERENCE_PLUGIN_PATH,
          package / mobagen::plugins::native_plugin_binary_filename()
      ));
      return package;
    }

  private:
    std::filesystem::path path_;
  };

  std::unique_ptr<mobagen::modules::LockedPluginActivationPlan> reference_plan(
      const std::filesystem::path& package, std::string_view configuration = {}
  ) {
    using namespace mobagen::modules;
    LockfileDocument document;
    document.metadata.plugins = {{
        .provider = "mobagen.reference",
        .version = {1, 0, 0},
        .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
        .package = "reference.plugin",
    }};
    document.resolved = {{
        .capability = MOBAGEN_RUNTIME_TICK_V1_ID,
        .provider = "mobagen.reference",
        .version = {1, 0, 0},
        .linkage = LinkageMode::Dynamic,
    }};
    if (!configuration.empty()) {
      const auto bytes = std::as_bytes(std::span{configuration.data(), configuration.size()});
      const auto digest = mobagen::assets::sha256(bytes);
      REQUIRE(digest.has_value());
      document.configurations = {{
          .provider = "mobagen.reference",
          .schema = "mobagen.reference.config.v1",
          .hash = mobagen::assets::to_string(*digest),
      }};
    }
    const std::vector verified_plugins{VerifiedLockedPlugin{
        .provider_id = "mobagen.reference",
        .version = {1, 0, 0},
        .linkage = LinkageMode::Dynamic,
        .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
        .size = 4096,
        .package_path = package,
        .binary_path = package / mobagen::plugins::native_plugin_binary_filename(),
    }};
    auto planned = build_locked_plugin_activation_plan(document, verified_plugins);
    REQUIRE(planned.ok());
    return std::move(planned.plan);
  }

  std::unique_ptr<mobagen::modules::LockedPluginActivationPlan> reference_and_unused_plan(
      const std::filesystem::path& reference_package
  ) {
    using namespace mobagen::modules;
    LockfileDocument document;
    document.metadata.plugins = {
        {.provider = "mobagen.reference",
         .version = {1, 0, 0},
         .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
         .package = "reference.plugin"},
        {.provider = "mobagen.unused",
         .version = {1, 0, 0},
         .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
         .package = "unused.plugin"},
    };
    document.resolved = {
        {.capability = MOBAGEN_RUNTIME_TICK_V1_ID,
         .provider = "mobagen.reference",
         .version = {1, 0, 0},
         .linkage = LinkageMode::Dynamic},
        {.capability = "unused.service.v1",
         .provider = "mobagen.unused",
         .version = {1, 0, 0},
         .linkage = LinkageMode::Dynamic},
    };
    const auto missing_package = std::filesystem::path{"missing/unused.plugin"};
    const std::vector verified_plugins{
        VerifiedLockedPlugin{
            .provider_id = "mobagen.unused",
            .version = {1, 0, 0},
            .linkage = LinkageMode::Dynamic,
            .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
            .size = 4096,
            .package_path = missing_package,
            .binary_path = missing_package / mobagen::plugins::native_plugin_binary_filename(),
        },
        VerifiedLockedPlugin{
            .provider_id = "mobagen.reference",
            .version = {1, 0, 0},
            .linkage = LinkageMode::Dynamic,
            .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
            .size = 4096,
            .package_path = reference_package,
            .binary_path = reference_package / mobagen::plugins::native_plugin_binary_filename(),
        },
    };
    auto planned = build_locked_plugin_activation_plan(document, verified_plugins);
    REQUIRE(planned.ok());
    return std::move(planned.plan);
  }

  mobagen::compositions::NativeModuleConfiguration configuration(std::string_view value) {
    const auto bytes = std::as_bytes(std::span{value.data(), value.size()});
    return {
        .provider_id = "mobagen.reference",
        .data = std::vector<std::byte>{bytes.begin(), bytes.end()},
    };
  }

}  // namespace

TEST_CASE("Native module manager: construction performs no plugin code loading") {
  using namespace mobagen;
  const auto missing_package = std::filesystem::path{"missing/reference.plugin"};
  auto created = compositions::create_native_module_manager(reference_plan(missing_package));

  REQUIRE(created.ok());
  CHECK(created.manager->active_count() == 0);
  CHECK(created.manager->host().size() == 0);

  const auto activated = created.manager->activate(MOBAGEN_RUNTIME_TICK_V1_ID);

  CHECK_FALSE(activated.ok());
  REQUIRE(activated.issues.size() == 1);
  CHECK(activated.issues.front().code == compositions::NativeModuleManagerIssueCode::LoadFailed);
  CHECK(activated.issues.front().provider_id == "mobagen.reference");
  CHECK(created.manager->active_count() == 0);
}

TEST_CASE("Native module manager: first capability request activates once and reuses the ABI table") {
  using namespace mobagen;
  TemporaryNativeModuleProject project;
  const std::vector configurations{configuration("41")};
  auto created = compositions::create_native_module_manager(
      reference_plan(project.add_reference_plugin(), "41"), configurations
  );
  REQUIRE(created.ok());

  REQUIRE(created.manager->activate(MOBAGEN_RUNTIME_TICK_V1_ID).ok());
  CHECK(created.manager->active_count() == 1);
  const auto api = created.manager->host().find<MobagenRuntimeTickV1>(
      MOBAGEN_RUNTIME_TICK_V1_ID, 1
  );
  REQUIRE(api.has_value());
  CHECK((*api)->tick((*api)->plugin_state) == MOBAGEN_STATUS_OK);
  CHECK((*api)->tick_count((*api)->plugin_state) == 42);

  REQUIRE(created.manager->activate(MOBAGEN_RUNTIME_TICK_V1_ID).ok());
  CHECK(created.manager->active_count() == 1);
  CHECK((*api)->tick_count((*api)->plugin_state) == 42);
  CHECK(created.manager->stop().ok());
  CHECK(created.manager->host().size() == 0);
}

TEST_CASE("Native module manager: capability acquisition returns the direct ABI table") {
  using namespace mobagen;
  TemporaryNativeModuleProject project;
  auto created = compositions::create_native_module_manager(
      reference_plan(project.add_reference_plugin())
  );
  REQUIRE(created.ok());

  const auto acquired = created.manager->acquire(MOBAGEN_RUNTIME_TICK_V1_ID, 1);

  REQUIRE(acquired.ok());
  REQUIRE(acquired.binding.has_value());
  CHECK(acquired.binding->provider_id == "mobagen.reference");
  CHECK(acquired.binding->capability_id == MOBAGEN_RUNTIME_TICK_V1_ID);
  CHECK(acquired.binding->abi_version == 1);
  REQUIRE(acquired.binding->function_table_size >= sizeof(MobagenRuntimeTickV1));
  const auto* api = static_cast<const MobagenRuntimeTickV1*>(
      acquired.binding->function_table
  );
  CHECK(api->tick(api->plugin_state) == MOBAGEN_STATUS_OK);
  CHECK(created.manager->active_count() == 1);

  const auto reused = created.manager->acquire(MOBAGEN_RUNTIME_TICK_V1_ID, 1);
  REQUIRE(reused.ok());
  CHECK(reused.binding->function_table == acquired.binding->function_table);
  CHECK(created.manager->active_count() == 1);
  CHECK(created.manager->stop().ok());
}

TEST_CASE("Native module manager: capability acquisition enforces the requested ABI") {
  using namespace mobagen;
  TemporaryNativeModuleProject project;
  auto created = compositions::create_native_module_manager(
      reference_plan(project.add_reference_plugin())
  );
  REQUIRE(created.ok());

  const auto acquired = created.manager->acquire(MOBAGEN_RUNTIME_TICK_V1_ID, 2);

  CHECK_FALSE(acquired.ok());
  CHECK_FALSE(acquired.binding.has_value());
  REQUIRE(acquired.issues.size() == 1);
  CHECK(acquired.issues.front().code
        == compositions::NativeModuleManagerIssueCode::UnsupportedCapabilityAbi);
  CHECK(created.manager->active_count() == 1);
  CHECK(created.manager->stop().ok());
}

TEST_CASE("Native module manager: configuration must match the locked digest before loading") {
  using namespace mobagen;
  const std::vector configurations{configuration("42")};

  const auto created = compositions::create_native_module_manager(
      reference_plan("missing/reference.plugin", "41"), configurations
  );

  CHECK_FALSE(created.ok());
  REQUIRE(created.issues.size() == 1);
  CHECK(created.issues.front().code
        == compositions::NativeModuleManagerIssueCode::InvalidConfiguration);
  CHECK(created.issues.front().provider_id == "mobagen.reference");
}

TEST_CASE("Native module manager: requesting one capability leaves unrelated selected code unloaded") {
  using namespace mobagen;
  TemporaryNativeModuleProject project;
  auto created = compositions::create_native_module_manager(
      reference_and_unused_plan(project.add_reference_plugin())
  );
  REQUIRE(created.ok());

  const auto activated = created.manager->activate(MOBAGEN_RUNTIME_TICK_V1_ID);

  REQUIRE(activated.ok());
  CHECK(created.manager->active_count() == 1);
  CHECK(created.manager->host().find<MobagenRuntimeTickV1>(MOBAGEN_RUNTIME_TICK_V1_ID, 1)
            .has_value());
  CHECK(created.manager->stop().ok());
}

TEST_CASE("Native module manager: another thread cannot trigger plugin code loading") {
  using namespace mobagen;
  TemporaryNativeModuleProject project;
  auto created = compositions::create_native_module_manager(
      reference_plan(project.add_reference_plugin())
  );
  REQUIRE(created.ok());
  compositions::NativeModuleManagerActionResult activated;

  std::thread worker{[&] {
    activated = created.manager->activate(MOBAGEN_RUNTIME_TICK_V1_ID);
  }};
  worker.join();

  CHECK_FALSE(activated.ok());
  REQUIRE(activated.issues.size() == 1);
  CHECK(activated.issues.front().code
        == compositions::NativeModuleManagerIssueCode::WrongThread);
  CHECK(created.manager->active_count() == 0);
  CHECK(created.manager->host().size() == 0);
}
