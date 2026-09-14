#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

#include "modules/resolver.hpp"
#include "plugins/plugin_activation_set.hpp"
#include "plugins/plugin_catalog.hpp"
#include "plugins/runtime_tick_v1.h"

namespace {

  class TemporaryResolvedPluginProject {
  public:
    TemporaryResolvedPluginProject() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-resolved-plugin-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directories(path_ / "plugins"));
    }

    ~TemporaryResolvedPluginProject() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    void add(std::string_view name, const std::filesystem::path& binary) const {
      const auto package = path_ / "plugins" / (std::string{name} + ".plugin");
      REQUIRE(std::filesystem::create_directory(package));
      REQUIRE(std::filesystem::copy_file(binary, package / mobagen::plugins::native_plugin_binary_filename()));
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

  mobagen::modules::ProductDescriptor product_for(std::string provider, std::string package) {
    return {
        .name = "resolved-plugin-test",
        .modules = {{.alias = "runtime", .provider = std::move(provider)}},
        .plugins = {std::move(package)},
        .profiles = {{.name = "release", .linkage = mobagen::modules::LinkageMode::Dynamic, .editor = false}},
    };
  }

  mobagen::modules::ResolutionResult resolve(const mobagen::modules::ProductDescriptor& product,
                                             const mobagen::modules::CapabilityRegistry& registry) {
    return mobagen::modules::resolve_modules(
        product, registry,
        {.target = native_target(), .profile = "release", .aliases = {{.alias = "runtime", .capability = MOBAGEN_RUNTIME_TICK_V1_ID}}});
  }

}  // namespace

TEST_CASE("Resolved plugin activation: manifest selection becomes a callable native service") {
  using namespace mobagen;
  TemporaryResolvedPluginProject project;
  project.add("reference", MOBAGEN_REFERENCE_PLUGIN_PATH);
  auto product = product_for("mobagen.reference", "plugins/reference.plugin");
  plugins::PluginHost host;
  auto catalog = plugins::discover_native_plugin_catalog(product, project.path(), host);
  REQUIRE(catalog.ok());
  auto resolution = resolve(product, catalog.catalog->registry());
  REQUIRE(resolution.ok());

  auto activated = plugins::activate_resolved_native_plugins(*catalog.catalog, *resolution.resolution, host);

  REQUIRE(activated.ok());
  CHECK(activated.activation->size() == 1);
  const auto api = host.find<MobagenRuntimeTickV1>(MOBAGEN_RUNTIME_TICK_V1_ID, 1);
  REQUIRE(api.has_value());
  CHECK((*api)->tick((*api)->plugin_state) == MOBAGEN_STATUS_OK);
  CHECK((*api)->tick_count((*api)->plugin_state) == 1);
  CHECK(activated.activation->stop().ok());
  CHECK(host.size() == 0);
}

TEST_CASE("Resolved plugin activation: selected lifecycle failure rolls back the entire set") {
  using namespace mobagen;
  TemporaryResolvedPluginProject project;
  project.add("failure", MOBAGEN_START_FAILURE_PLUGIN_PATH);
  auto product = product_for("mobagen.lifecycle-failure", "plugins/failure.plugin");
  plugins::PluginHost host;
  auto catalog = plugins::discover_native_plugin_catalog(product, project.path(), host);
  REQUIRE(catalog.ok());
  auto resolution = resolve(product, catalog.catalog->registry());
  REQUIRE(resolution.ok());

  const auto activated = plugins::activate_resolved_native_plugins(*catalog.catalog, *resolution.resolution, host);

  CHECK_FALSE(activated.ok());
  CHECK_FALSE(activated.issues.empty());
  CHECK(activated.issues.front().provider_id == "mobagen.lifecycle-failure");
  CHECK(host.size() == 0);
}

TEST_CASE("Resolved plugin activation: discovered but unselected plugins remain inactive") {
  using namespace mobagen;
  TemporaryResolvedPluginProject project;
  project.add("reference", MOBAGEN_REFERENCE_PLUGIN_PATH);
  auto product = product_for("mobagen.runtime.builtin", "plugins/reference.plugin");
  product.profiles.front().linkage = modules::LinkageMode::Static;
  const modules::ProviderDescriptor builtin{
      .id = "mobagen.runtime.builtin",
      .version = {1, 0, 0},
      .provides = {MOBAGEN_RUNTIME_TICK_V1_ID},
      .targets = {native_target()},
      .linkages = {modules::LinkageMode::Static},
  };
  plugins::PluginHost host;
  auto catalog = plugins::discover_native_plugin_catalog(product, project.path(), host, std::span(&builtin, 1));
  REQUIRE(catalog.ok());
  auto resolution = resolve(product, catalog.catalog->registry());
  REQUIRE(resolution.ok());

  const auto activated = plugins::activate_resolved_native_plugins(*catalog.catalog, *resolution.resolution, host);

  REQUIRE(activated.ok());
  CHECK(activated.activation->size() == 0);
  CHECK(catalog.catalog->plugin_count() == 1);
  CHECK(host.size() == 0);
}

TEST_CASE("Resolved plugin activation: a resolution from another registry is rejected before consumption") {
  using namespace mobagen;
  TemporaryResolvedPluginProject project;
  project.add("reference", MOBAGEN_REFERENCE_PLUGIN_PATH);
  auto product = product_for("mobagen.reference", "plugins/reference.plugin");
  plugins::PluginHost host;
  auto source_catalog = plugins::discover_native_plugin_catalog(product, project.path(), host);
  REQUIRE(source_catalog.ok());
  auto resolution = resolve(product, source_catalog.catalog->registry());
  REQUIRE(resolution.ok());

  modules::ProductDescriptor other_product{.name = "other-catalog"};
  const modules::ProviderDescriptor other_provider{
      .id = "mobagen.other",
      .version = {1, 0, 0},
      .provides = {"runtime.other.v1"},
      .targets = {native_target()},
      .linkages = {modules::LinkageMode::Dynamic},
  };
  auto other_catalog = plugins::discover_native_plugin_catalog(other_product, project.path(), host, std::span(&other_provider, 1));
  REQUIRE(other_catalog.ok());

  const auto activated = plugins::activate_resolved_native_plugins(*other_catalog.catalog, *resolution.resolution, host);

  CHECK_FALSE(activated.ok());
  REQUIRE_FALSE(activated.issues.empty());
  CHECK(activated.issues.front().code == plugins::ResolvedNativePluginIssueCode::InvalidResolution);
  CHECK(source_catalog.catalog->plugin_count() == 1);
  CHECK(host.size() == 0);
}
