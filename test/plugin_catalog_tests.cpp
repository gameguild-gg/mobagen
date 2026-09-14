#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

#include "plugins/plugin_catalog.hpp"

namespace {

  class TemporaryCatalogProject {
  public:
    TemporaryCatalogProject() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      root_ = std::filesystem::temp_directory_path()
              / ("mobagen-plugin-catalog-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      project_ = root_ / "project";
      REQUIRE(std::filesystem::create_directories(project_ / "plugins"));
    }

    ~TemporaryCatalogProject() {
      std::error_code error;
      std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] const std::filesystem::path& project() const noexcept { return project_; }
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

    [[nodiscard]] std::filesystem::path package(const std::filesystem::path& parent, std::string_view name,
                                                const std::filesystem::path& binary = MOBAGEN_REFERENCE_PLUGIN_PATH) const {
      const auto package = parent / (std::string{name} + ".plugin");
      REQUIRE(std::filesystem::create_directory(package));
      REQUIRE(std::filesystem::copy_file(binary, package / mobagen::plugins::native_plugin_binary_filename()));
      return package;
    }

  private:
    std::filesystem::path root_;
    std::filesystem::path project_;
  };

  bool has_issue(const mobagen::plugins::NativePluginCatalogResult& result, mobagen::plugins::NativePluginCatalogIssueCode code) {
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
  }

}  // namespace

TEST_CASE("Plugin catalog: manifest paths discover native providers into one deterministic registry") {
  using namespace mobagen;
  TemporaryCatalogProject project;
  (void)project.package(project.project() / "plugins", "reference");
  modules::ProductDescriptor product{.name = "catalog-test", .plugins = {"./plugins/reference.plugin"}};
  const modules::ProviderDescriptor builtin{
      .id = "mobagen.runtime.builtin",
      .version = {1, 0, 0},
      .provides = {"runtime.builtin.v1"},
      .targets = {modules::TargetPlatform::Windows},
      .linkages = {modules::LinkageMode::Static},
  };
  plugins::PluginHost host;

  auto result = plugins::discover_native_plugin_catalog(product, project.project(), host, std::span(&builtin, 1));

  REQUIRE(result.ok());
  CHECK(result.catalog->plugin_count() == 1);
  REQUIRE(result.catalog->plugin(0) != nullptr);
  CHECK(result.catalog->plugin(0)->contract().provider.id == "mobagen.reference");
  CHECK(result.catalog->registry().provider_count() == 2);
  CHECK(result.catalog->registry().find_provider("mobagen.reference").has_value());
  CHECK(result.catalog->registry().find_provider("mobagen.runtime.builtin").has_value());
}

TEST_CASE("Plugin catalog: paths cannot escape the project root") {
  using namespace mobagen;
  TemporaryCatalogProject project;
  (void)project.package(project.root(), "outside");
  modules::ProductDescriptor product{.name = "catalog-test", .plugins = {"../outside.plugin"}};
  plugins::PluginHost host;

  const auto result = plugins::discover_native_plugin_catalog(product, project.project(), host);

  CHECK_FALSE(result.ok());
  CHECK(has_issue(result, plugins::NativePluginCatalogIssueCode::PathOutsideProject));
  CHECK(host.size() == 0);
}

TEST_CASE("Plugin catalog: normalized duplicate packages are rejected before loading twice") {
  using namespace mobagen;
  TemporaryCatalogProject project;
  (void)project.package(project.project() / "plugins", "reference");
  modules::ProductDescriptor product{
      .name = "catalog-test",
      .plugins = {"plugins/reference.plugin", "./plugins/../plugins/reference.plugin"},
  };
  plugins::PluginHost host;

  const auto result = plugins::discover_native_plugin_catalog(product, project.project(), host);

  CHECK_FALSE(result.ok());
  CHECK(has_issue(result, plugins::NativePluginCatalogIssueCode::DuplicatePackage));
}

TEST_CASE("Plugin catalog: registry failure discards all loaded binaries") {
  using namespace mobagen;
  TemporaryCatalogProject project;
  const auto first = project.package(project.project() / "plugins", "first");
  const auto second = project.package(project.project() / "plugins", "second");
  modules::ProductDescriptor product{
      .name = "catalog-test",
      .plugins = {"plugins/first.plugin", "plugins/second.plugin"},
  };
  plugins::PluginHost host;

  auto result = plugins::discover_native_plugin_catalog(product, project.project(), host);

  CHECK_FALSE(result.ok());
  CHECK(has_issue(result, plugins::NativePluginCatalogIssueCode::RegistryFailed));
  std::error_code error;
  CHECK(std::filesystem::remove(first / plugins::native_plugin_binary_filename(), error));
  CHECK_FALSE(error);
  error.clear();
  CHECK(std::filesystem::remove(second / plugins::native_plugin_binary_filename(), error));
  CHECK_FALSE(error);
}

TEST_CASE("Plugin catalog: load failure discards binaries loaded earlier in deterministic order") {
  using namespace mobagen;
  TemporaryCatalogProject project;
  const auto first = project.package(project.project() / "plugins", "first");
  modules::ProductDescriptor product{
      .name = "catalog-test",
      .plugins = {"plugins/missing.plugin", "plugins/first.plugin"},
  };
  plugins::PluginHost host;

  auto result = plugins::discover_native_plugin_catalog(product, project.project(), host);

  CHECK_FALSE(result.ok());
  CHECK(has_issue(result, plugins::NativePluginCatalogIssueCode::LoadFailed));
  std::error_code error;
  CHECK(std::filesystem::remove(first / plugins::native_plugin_binary_filename(), error));
  CHECK_FALSE(error);
}
