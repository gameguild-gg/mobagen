#include <doctest/doctest.h>

#include "modules/locked_activation_plan.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

  mobagen::modules::SemanticVersion version(std::uint32_t major = 1) {
    return {major, 0, 0};
  }

  mobagen::modules::LockfileDocument locked_project() {
    using namespace mobagen::modules;
    LockfileDocument document;
    document.metadata.plugins = {
        {.provider = "mobagen.render",
         .version = version(),
         .abi_version = 1,
         .package = ".mobagen/plugins/render.plugin"},
        {.provider = "mobagen.platform",
         .version = version(),
         .abi_version = 1,
         .package = ".mobagen/plugins/platform.plugin"},
    };
    document.resolved = {
        {.capability = "render.backend.v1",
         .provider = "mobagen.render",
         .version = version(),
         .linkage = LinkageMode::Dynamic},
        {.capability = "window.surface.v1",
         .provider = "mobagen.platform",
         .version = version(),
         .linkage = LinkageMode::Dynamic},
    };
    document.dependencies = {
        {.capability = "window.surface.v1", .provider = "mobagen.platform", .required_by = "mobagen.render"},
    };
    return document;
  }

  mobagen::modules::VerifiedLockedPlugin verified(
      std::string provider, std::string package, std::string binary
  ) {
    using namespace mobagen::modules;
    return {
        .provider_id = std::move(provider),
        .version = version(),
        .linkage = LinkageMode::Dynamic,
        .abi_version = 1,
        .size = 4096,
        .package_path = std::filesystem::path{std::move(package)},
        .binary_path = std::filesystem::path{std::move(binary)},
    };
  }

}  // namespace

TEST_CASE("Locked activation plan: metadata orders selected plugins without touching binaries") {
  using namespace mobagen::modules;
  const auto document = locked_project();
  const std::vector plugins{
      verified("mobagen.render", "missing/render.plugin", "missing/render.plugin/plugin.dll"),
      verified("mobagen.platform", "missing/platform.plugin", "missing/platform.plugin/plugin.dll"),
  };

  const auto result = build_locked_plugin_activation_plan(document, plugins);

  REQUIRE(result.ok());
  REQUIRE(result.plan->entries().size() == 2);
  CHECK(result.plan->entries()[0].provider_id == "mobagen.platform");
  CHECK(result.plan->entries()[1].provider_id == "mobagen.render");
  CHECK(result.plan->entries()[0].capabilities == std::vector<std::string>{"window.surface.v1"});
  CHECK(result.plan->entries()[1].capabilities == std::vector<std::string>{"render.backend.v1"});
  REQUIRE(result.plan->entries()[1].dependencies.size() == 1);
  CHECK(result.plan->entries()[1].dependencies[0].provider_id == "mobagen.platform");
  CHECK(result.plan->entries()[1].dependencies[0].capability == "window.surface.v1");
  CHECK(result.plan->find("mobagen.render") == &result.plan->entries()[1]);
  CHECK(result.plan->find("mobagen.unknown") == nullptr);
}

TEST_CASE("Locked activation plan: verified packages must exactly cover selected dynamic plugins") {
  using namespace mobagen::modules;
  const auto document = locked_project();
  const std::vector plugins{
      verified("mobagen.platform", "missing/platform.plugin", "missing/platform.plugin/plugin.dll"),
  };

  const auto result = build_locked_plugin_activation_plan(document, plugins);

  CHECK_FALSE(result.ok());
  REQUIRE(result.issues.size() == 1);
  CHECK(result.issues.front().code == LockedActivationPlanIssueCode::MissingVerifiedPlugin);
  CHECK(result.issues.front().provider_id == "mobagen.render");
}

TEST_CASE("Locked activation plan: cyclic locked dependencies are rejected") {
  using namespace mobagen::modules;
  auto document = locked_project();
  document.dependencies.push_back(
      {.capability = "render.backend.v1", .provider = "mobagen.render", .required_by = "mobagen.platform"}
  );
  const std::vector plugins{
      verified("mobagen.render", "missing/render.plugin", "missing/render.plugin/plugin.dll"),
      verified("mobagen.platform", "missing/platform.plugin", "missing/platform.plugin/plugin.dll"),
  };

  const auto result = build_locked_plugin_activation_plan(document, plugins);

  CHECK_FALSE(result.ok());
  REQUIRE(result.issues.size() == 1);
  CHECK(result.issues.front().code == LockedActivationPlanIssueCode::DependencyCycle);
}
