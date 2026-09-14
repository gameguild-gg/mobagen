#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "assets/asset_id.hpp"
#include "portable/project_runtime.hpp"
#include "plugins/wasm_plugin_activation_set.hpp"
#include "support/wasm_plugin_test_support.hpp"

namespace {

  using namespace mobagen::test;

  std::uint32_t capture_import_log(void* state, std::uint32_t, std::string_view message) {
    *static_cast<std::string*>(state) = message;
    return MOBAGEN_WASM_STATUS_OK;
  }

  bool has_issue(const mobagen::plugins::PortableWasmPluginLoadResult& result, mobagen::plugins::PortableWasmPluginLoadIssueCode code) {
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
  }

  bool has_issue(const mobagen::plugins::PortableWasmPluginCatalogResult& result, mobagen::plugins::PortableWasmPluginCatalogIssueCode code) {
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
  }

}  // namespace

TEST_CASE("Portable WASM plugin loader: a bounded module is instantiated and queried") {
  TemporaryWasmDirectory directory;
  const auto binary = directory.path() / "reference.wasm";
  write_binary(binary, valid_wasm_header);
  FakeWasmBackend backend;

  auto result = mobagen::plugins::load_portable_wasm_plugin_binary(binary, backend);

  REQUIRE(result.plugin.has_value());
  CHECK(result.issues.empty());
  CHECK(result.plugin->loaded());
  CHECK(result.plugin->path() == std::filesystem::absolute(binary));
  CHECK(result.plugin->provider().id == "mobagen.wasm-package");
  CHECK(result.plugin->provider().linkages == std::vector{mobagen::modules::LinkageMode::Wasm});
  CHECK(backend.calls == 1);
  CHECK(backend.observed == std::vector<std::byte>{valid_wasm_header.begin(), valid_wasm_header.end()});
  auto instance = result.plugin->take_instance();
  REQUIRE(instance != nullptr);
  REQUIRE(backend.host_imports.size() == 1);
  CHECK(instance->host_imports() == backend.host_imports.front());
  CHECK_FALSE(result.plugin->loaded());
}

TEST_CASE("Portable WASM plugin loader: the instance owns its injected host imports") {
  TemporaryWasmDirectory directory;
  const auto binary = directory.path() / "imports.wasm";
  write_binary(binary, valid_wasm_header);
  FakeWasmBackend backend;
  std::string message;
  const mobagen::plugins::WasmHostServices services{.state = &message, .log = capture_import_log};

  auto result = mobagen::plugins::load_portable_wasm_plugin_binary(binary, backend, services);

  REQUIRE(result.plugin.has_value());
  auto instance = result.plugin->take_instance();
  REQUIRE(instance != nullptr);
  REQUIRE(instance->host_imports() != nullptr);
  CHECK(instance->host_imports()->log(instance->memory(), 2, 96, 20) == MOBAGEN_WASM_STATUS_OK);
  CHECK(message == "mobagen.wasm-package");
}

TEST_CASE("Portable WASM plugin loader: malformed and oversized files fail before the backend") {
  TemporaryWasmDirectory directory;
  FakeWasmBackend backend;

  const auto missing = mobagen::plugins::load_portable_wasm_plugin_binary(directory.path() / "missing.wasm", backend);
  CHECK(has_issue(missing, mobagen::plugins::PortableWasmPluginLoadIssueCode::OpenFailed));

  const auto malformed_path = directory.path() / "malformed.wasm";
  auto malformed_header = valid_wasm_header;
  malformed_header[1] = std::byte{0xff};
  write_binary(malformed_path, malformed_header);
  const auto malformed = mobagen::plugins::load_portable_wasm_plugin_binary(malformed_path, backend);
  CHECK(has_issue(malformed, mobagen::plugins::PortableWasmPluginLoadIssueCode::InvalidBinary));

  const auto unsupported_path = directory.path() / "unsupported.wasm";
  auto unsupported_header = valid_wasm_header;
  unsupported_header[4] = std::byte{0x02};
  write_binary(unsupported_path, unsupported_header);
  const auto unsupported = mobagen::plugins::load_portable_wasm_plugin_binary(unsupported_path, backend);
  CHECK(has_issue(unsupported, mobagen::plugins::PortableWasmPluginLoadIssueCode::UnsupportedVersion));

  const auto oversized_path = directory.path() / "oversized.wasm";
  std::ofstream oversized(oversized_path, std::ios::binary | std::ios::trunc);
  REQUIRE(oversized.is_open());
  oversized.seekp(static_cast<std::streamoff>(mobagen::plugins::max_portable_wasm_plugin_binary_bytes));
  oversized.put('\0');
  oversized.close();
  const auto too_large = mobagen::plugins::load_portable_wasm_plugin_binary(oversized_path, backend);
  CHECK(has_issue(too_large, mobagen::plugins::PortableWasmPluginLoadIssueCode::SizeLimit));
  CHECK(backend.calls == 0);
}

TEST_CASE("Portable WASM plugin loader: backend and descriptor failures remain structured") {
  TemporaryWasmDirectory directory;
  const auto binary = directory.path() / "reference.wasm";
  write_binary(binary, valid_wasm_header);

  FakeWasmBackend rejected;
  rejected.fails = true;
  const auto backend_failure = mobagen::plugins::load_portable_wasm_plugin_binary(binary, rejected);
  CHECK(has_issue(backend_failure, mobagen::plugins::PortableWasmPluginLoadIssueCode::BackendFailure));

  FakeWasmBackend throwing;
  throwing.throws = true;
  const auto backend_trap = mobagen::plugins::load_portable_wasm_plugin_binary(binary, throwing);
  CHECK(has_issue(backend_trap, mobagen::plugins::PortableWasmPluginLoadIssueCode::BackendFailure));

  FakeWasmBackend detached;
  detached.drops_host_imports = true;
  const auto detached_imports = mobagen::plugins::load_portable_wasm_plugin_binary(binary, detached);
  CHECK(has_issue(detached_imports, mobagen::plugins::PortableWasmPluginLoadIssueCode::BackendFailure));

  FakeWasmBackend malformed;
  malformed.malformed_descriptor = true;
  const auto query_failure = mobagen::plugins::load_portable_wasm_plugin_binary(binary, malformed);
  CHECK(has_issue(query_failure, mobagen::plugins::PortableWasmPluginLoadIssueCode::QueryFailed));
  REQUIRE(query_failure.issues.size() == 1);
  CHECK_FALSE(query_failure.issues[0].query_issues.empty());
}

TEST_CASE("Portable WASM plugin loader: queried metadata transfers into activation without a second query") {
  TemporaryWasmDirectory directory;
  const auto binary = directory.path() / "reference.wasm";
  write_binary(binary, valid_wasm_header);
  FakeWasmBackend backend;
  auto loaded = mobagen::plugins::load_portable_wasm_plugin_binary(binary, backend);
  REQUIRE(loaded.plugin.has_value());

  auto activated = mobagen::plugins::activate_loaded_portable_wasm_plugin(std::move(*loaded.plugin));

  REQUIRE(activated.activation != nullptr);
  CHECK(activated.activation->provider().id == "mobagen.wasm-package");
  CHECK(activated.activation->state() == mobagen::plugins::PortableWasmPluginState::Active);
  REQUIRE(backend.host_imports.size() == 1);
  REQUIRE(backend.host_imports.front() != nullptr);
  CHECK(backend.host_imports.front()->bound());
  CHECK(backend.host_imports.front()->permissions().empty());
  CHECK(std::ranges::count(*backend.invocations, mobagen::plugins::WasmPluginExport::Query) == 1);
  CHECK(backend.invocations->back() == mobagen::plugins::WasmPluginExport::Start);
  CHECK(activated.activation->quiesce().ok());
  CHECK(activated.activation->stop().ok());
  CHECK_FALSE(backend.host_imports.front()->bound());
}

TEST_CASE("Portable WASM plugin loader: standalone activation denies unresolved permissions") {
  TemporaryWasmDirectory directory;
  const auto binary = directory.path() / "privileged.wasm";
  write_binary(binary, valid_wasm_header);
  FakeWasmBackend backend;
  backend.permission_ids = {"gpu"};
  auto loaded = mobagen::plugins::load_portable_wasm_plugin_binary(binary, backend);
  REQUIRE(loaded.plugin.has_value());

  const auto activated = mobagen::plugins::activate_loaded_portable_wasm_plugin(std::move(*loaded.plugin));

  CHECK_FALSE(activated.ok());
  REQUIRE(activated.issues.size() == 1);
  CHECK(activated.issues.front().code == mobagen::plugins::PortableWasmPluginIssueCode::HostImportsFailed);
  CHECK(invocation_count(backend, mobagen::plugins::WasmPluginExport::Configure) == 0);
  CHECK(invocation_count(backend, mobagen::plugins::WasmPluginExport::Start) == 0);
}

TEST_CASE("Portable WASM plugin loader: dot-plugin package shape is strict") {
  TemporaryWasmDirectory directory;
  FakeWasmBackend backend;
  const auto package = directory.path() / "reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / mobagen::plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);

  const auto valid = mobagen::plugins::load_portable_wasm_plugin_package(package, backend);
  REQUIRE(valid.plugin.has_value());
  CHECK(valid.plugin->provider().id == "mobagen.wasm-package");

  std::ofstream(package / "README.txt") << "unexpected";
  const auto extra = mobagen::plugins::load_portable_wasm_plugin_package(package, backend);
  CHECK(has_issue(extra, mobagen::plugins::PortableWasmPluginLoadIssueCode::InvalidPackage));

  const auto wrong_extension = directory.path() / "reference.bundle";
  REQUIRE(std::filesystem::create_directory(wrong_extension));
  const auto wrong = mobagen::plugins::load_portable_wasm_plugin_package(wrong_extension, backend);
  CHECK(has_issue(wrong, mobagen::plugins::PortableWasmPluginLoadIssueCode::InvalidPackage));

  const auto missing_package = directory.path() / "missing.plugin";
  REQUIRE(std::filesystem::create_directory(missing_package));
  const auto missing = mobagen::plugins::load_portable_wasm_plugin_package(missing_package, backend);
  CHECK(has_issue(missing, mobagen::plugins::PortableWasmPluginLoadIssueCode::MissingPackageBinary));
}

TEST_CASE("Portable WASM plugin catalog: manifest packages join builtins in one registry") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  const auto plugin_directory = directory.path() / "plugins";
  REQUIRE(std::filesystem::create_directory(plugin_directory));
  const auto package = plugin_directory / "reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  modules::ProductDescriptor product{.name = "wasm-catalog", .plugins = {"plugins/reference.plugin"}};
  const modules::ProviderDescriptor builtin{
      .id = "mobagen.runtime.builtin",
      .version = {1, 0, 0},
      .provides = {"runtime.builtin.v1"},
      .targets = {
          modules::TargetPlatform::Windows,
          modules::TargetPlatform::Linux,
          modules::TargetPlatform::MacOS,
          modules::TargetPlatform::Web,
          modules::TargetPlatform::Android,
          modules::TargetPlatform::IOS,
      },
      .linkages = {modules::LinkageMode::Static},
  };
  FakeWasmBackend backend;

  auto result = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend, std::span(&builtin, 1));

  REQUIRE(result.ok());
  CHECK(result.catalog->plugin_count() == 1);
  REQUIRE(result.catalog->plugin(0) != nullptr);
  CHECK(result.catalog->plugin(0)->provider().id == "mobagen.wasm-package");
  CHECK(result.catalog->registry().provider_count() == 2);
  CHECK(result.catalog->registry().find_provider("mobagen.wasm-package").has_value());
  auto taken = result.catalog->take_plugin("mobagen.wasm-package");
  REQUIRE(taken.has_value());
  CHECK(taken->loaded());
  CHECK(result.catalog->plugin_count() == 0);
}

TEST_CASE("Portable WASM plugin catalog: paths cannot escape or name one package twice") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  const auto project = directory.path() / "project";
  REQUIRE(std::filesystem::create_directories(project / "plugins"));
  const auto package = project / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  const auto outside = directory.path() / "outside.plugin";
  REQUIRE(std::filesystem::create_directory(outside));
  write_binary(outside / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  FakeWasmBackend backend;

  const modules::ProductDescriptor escaped{.name = "wasm-catalog", .plugins = {"../outside.plugin"}};
  const auto escaped_result = plugins::discover_portable_wasm_plugin_catalog(escaped, project, backend);
  CHECK(has_issue(escaped_result, plugins::PortableWasmPluginCatalogIssueCode::PathOutsideProject));

  const modules::ProductDescriptor duplicate{
      .name = "wasm-catalog",
      .plugins = {"plugins/reference.plugin", "./plugins/../plugins/reference.plugin"},
  };
  const auto duplicate_result = plugins::discover_portable_wasm_plugin_catalog(duplicate, project, backend);
  CHECK(has_issue(duplicate_result, plugins::PortableWasmPluginCatalogIssueCode::DuplicatePackage));
  CHECK(backend.calls == 0);
}

TEST_CASE("Portable WASM plugin catalog: packages load in canonical path order") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  const auto plugin_directory = directory.path() / "plugins";
  REQUIRE(std::filesystem::create_directory(plugin_directory));
  for (const std::string_view name : {"z-last", "a-first"}) {
    const auto package = plugin_directory / (std::string{name} + ".plugin");
    REQUIRE(std::filesystem::create_directory(package));
    write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  }
  const modules::ProductDescriptor product{
      .name = "wasm-catalog",
      .plugins = {"plugins/z-last.plugin", "plugins/a-first.plugin"},
  };
  FakeWasmBackend backend;
  backend.provider_ids = {"mobagen.a-first", "mobagen.z-last"};

  const auto result = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend);

  REQUIRE(result.ok());
  REQUIRE(result.catalog->plugin_count() == 2);
  REQUIRE(result.catalog->plugin(0) != nullptr);
  REQUIRE(result.catalog->plugin(1) != nullptr);
  CHECK(result.catalog->plugin(0)->path().parent_path().filename() == "a-first.plugin");
  CHECK(result.catalog->plugin(0)->provider().id == "mobagen.a-first");
  CHECK(result.catalog->plugin(1)->path().parent_path().filename() == "z-last.plugin");
  CHECK(result.catalog->plugin(1)->provider().id == "mobagen.z-last");
}

TEST_CASE("Portable WASM plugin catalog: duplicate provider metadata rejects the staged registry") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  const auto plugin_directory = directory.path() / "plugins";
  REQUIRE(std::filesystem::create_directory(plugin_directory));
  for (const std::string_view name : {"first", "second"}) {
    const auto package = plugin_directory / (std::string{name} + ".plugin");
    REQUIRE(std::filesystem::create_directory(package));
    write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  }
  const modules::ProductDescriptor product{
      .name = "wasm-catalog",
      .plugins = {"plugins/first.plugin", "plugins/second.plugin"},
  };
  FakeWasmBackend backend;

  const auto result = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend);

  CHECK_FALSE(result.ok());
  CHECK(has_issue(result, plugins::PortableWasmPluginCatalogIssueCode::RegistryFailed));
  CHECK(backend.calls == 2);
}

TEST_CASE("Resolved portable WASM plugin activation: manifest selection activates without requery") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  const auto package = directory.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  modules::ProductDescriptor product{
      .name = "resolved-wasm",
      .modules = {{.alias = "runtime", .provider = "mobagen.wasm-package"}},
      .plugins = {"plugins/reference.plugin"},
      .profiles = {{.name = "release", .linkage = modules::LinkageMode::Wasm, .editor = false}},
  };
  FakeWasmBackend backend;
  backend.permission_ids = {"gpu"};
  product.profiles.front().permissions = {"gpu"};
  const modules::ProviderDescriptor builtin{
      .id = "mobagen.render.builtin",
      .version = {1, 0, 0},
      .provides = {"render.backend.v1"},
      .targets = {portable_target()},
      .linkages = {modules::LinkageMode::Wasm},
  };
  auto catalog = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend, std::span(&builtin, 1));
  REQUIRE(catalog.ok());
  const auto target = catalog.catalog->plugin(0)->provider().targets.front();
  const auto resolution
      = modules::resolve_modules(product, catalog.catalog->registry(),
                                 {.target = target, .profile = "release", .aliases = {{.alias = "runtime", .capability = "runtime.package.v1"}}});
  REQUIRE(resolution.ok());

  auto activated = plugins::activate_resolved_portable_wasm_plugins(*catalog.catalog, *resolution.resolution);

  REQUIRE(activated.ok());
  CHECK(activated.activation->size() == 1);
  REQUIRE(activated.activation->plugin(0) != nullptr);
  CHECK(activated.activation->plugin(0)->provider().id == "mobagen.wasm-package");
  REQUIRE(backend.host_imports.size() == 1);
  REQUIRE(backend.host_imports.front() != nullptr);
  CHECK(backend.host_imports.front()->bound());
  CHECK(std::ranges::equal(backend.host_imports.front()->permissions(), std::array{std::string{"gpu"}}));
  std::vector<std::byte> import_memory(128);
  constexpr std::string_view builtin_capability = "render.backend.v1";
  write_string(import_memory, 8, builtin_capability);
  CHECK(backend.host_imports.front()->find_capability(import_memory, 8, static_cast<std::uint32_t>(builtin_capability.size()), 1, 64)
        == MOBAGEN_WASM_STATUS_OK);
  CHECK(activated.activation->plugin(1) == nullptr);
  CHECK(catalog.catalog->plugin_count() == 0);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Query) == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Configure) == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Start) == 1);
  CHECK(activated.activation->stop().ok());
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Quiesce) == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Stop) == 1);
  CHECK_FALSE(backend.host_imports.front()->bound());

  const auto repeated = plugins::activate_resolved_portable_wasm_plugins(*catalog.catalog, *resolution.resolution);
  CHECK_FALSE(repeated.ok());
  REQUIRE_FALSE(repeated.issues.empty());
  CHECK(repeated.issues.front().code == plugins::ResolvedPortableWasmPluginIssueCode::PluginUnavailable);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Configure) == 1);
}

TEST_CASE("Resolved portable WASM plugin activation: selected failure rolls back the entire set") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  for (const std::string_view name : {"first", "second"}) {
    const auto package = directory.path() / "plugins" / (std::string{name} + ".plugin");
    REQUIRE(std::filesystem::create_directory(package));
    write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  }
  const modules::ProductDescriptor product{
      .name = "resolved-wasm",
      .modules = {
          {.alias = "first", .provider = "mobagen.first"},
          {.alias = "second", .provider = "mobagen.second"},
      },
      .plugins = {"plugins/first.plugin", "plugins/second.plugin"},
      .profiles = {{.name = "release", .linkage = modules::LinkageMode::Wasm, .editor = false}},
  };
  FakeWasmBackend backend;
  backend.provider_ids = {"mobagen.first", "mobagen.second"};
  backend.capability_ids = {"runtime.first.v1", "runtime.second.v1"};
  backend.start_statuses = {MOBAGEN_WASM_STATUS_OK, MOBAGEN_WASM_STATUS_FAILED};
  auto catalog = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend);
  REQUIRE(catalog.ok());
  const auto target = catalog.catalog->plugin(0)->provider().targets.front();
  const auto resolution = modules::resolve_modules(product, catalog.catalog->registry(),
                                                   {.target = target,
                                                    .profile = "release",
                                                    .aliases = {
                                                        {.alias = "first", .capability = "runtime.first.v1"},
                                                        {.alias = "second", .capability = "runtime.second.v1"},
                                                    }});
  REQUIRE(resolution.ok());

  const auto activated = plugins::activate_resolved_portable_wasm_plugins(*catalog.catalog, *resolution.resolution);

  CHECK_FALSE(activated.ok());
  REQUIRE_FALSE(activated.issues.empty());
  CHECK(activated.issues.front().code == plugins::ResolvedPortableWasmPluginIssueCode::ActivationFailed);
  CHECK(activated.issues.front().provider_id == "mobagen.second");
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Configure) == 2);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Start) == 2);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Quiesce) == 2);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Stop) == 2);
}

TEST_CASE("Resolved portable WASM plugin activation: builtins remain outside the plugin lifecycle") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  const auto package = directory.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  modules::ProductDescriptor product{
      .name = "resolved-wasm",
      .modules = {{.alias = "runtime", .provider = "mobagen.builtin"}},
      .plugins = {"plugins/reference.plugin"},
      .profiles = {{.name = "release", .linkage = modules::LinkageMode::Wasm, .editor = false}},
  };
  FakeWasmBackend backend;
  const modules::ProviderDescriptor builtin{
      .id = "mobagen.builtin",
      .version = {1, 0, 0},
      .provides = {"runtime.package.v1"},
      .targets = {
          modules::TargetPlatform::Windows,
          modules::TargetPlatform::Linux,
          modules::TargetPlatform::MacOS,
          modules::TargetPlatform::Web,
          modules::TargetPlatform::Android,
          modules::TargetPlatform::IOS,
      },
      .linkages = {modules::LinkageMode::Wasm},
  };
  auto catalog = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend, std::span(&builtin, 1));
  REQUIRE(catalog.ok());
  const auto runtime_target = catalog.catalog->plugin(0)->provider().targets.front();
  const auto resolution = modules::resolve_modules(
      product, catalog.catalog->registry(),
      {.target = runtime_target, .profile = "release", .aliases = {{.alias = "runtime", .capability = "runtime.package.v1"}}});
  REQUIRE(resolution.ok());

  const auto activated = plugins::activate_resolved_portable_wasm_plugins(*catalog.catalog, *resolution.resolution);

  REQUIRE(activated.ok());
  CHECK(activated.activation->size() == 0);
  CHECK(catalog.catalog->plugin_count() == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Configure) == 0);
}

TEST_CASE("Resolved portable WASM plugin activation: foreign resolutions are rejected before consumption") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  const auto package = directory.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  const modules::ProductDescriptor product{
      .name = "resolved-wasm",
      .modules = {{.alias = "runtime", .provider = "mobagen.wasm-package"}},
      .plugins = {"plugins/reference.plugin"},
      .profiles = {{.name = "release", .linkage = modules::LinkageMode::Wasm, .editor = false}},
  };
  FakeWasmBackend backend;
  auto catalog = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend);
  REQUIRE(catalog.ok());
  modules::CapabilityRegistryBuilder other_builder;
  other_builder.add(catalog.catalog->plugin(0)->provider());
  const auto other_registry = other_builder.build();
  REQUIRE(other_registry.ok());
  const auto target = catalog.catalog->plugin(0)->provider().targets.front();
  const auto resolution
      = modules::resolve_modules(product, *other_registry.registry,
                                 {.target = target, .profile = "release", .aliases = {{.alias = "runtime", .capability = "runtime.package.v1"}}});
  REQUIRE(resolution.ok());

  const auto activated = plugins::activate_resolved_portable_wasm_plugins(*catalog.catalog, *resolution.resolution);

  CHECK_FALSE(activated.ok());
  REQUIRE_FALSE(activated.issues.empty());
  CHECK(activated.issues.front().code == plugins::ResolvedPortableWasmPluginIssueCode::InvalidResolution);
  CHECK(catalog.catalog->plugin_count() == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Configure) == 0);
}

TEST_CASE("Portable project: mobagen yaml resolves and activates a dot-plugin end to end") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  const auto package = directory.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  write_text(directory.path() / "mobagen.yaml", R"yaml(schema: 1
name: portable-project-test
modules:
  runtime:
    use: mobagen.wasm-package
plugins:
  - ./plugins/reference.plugin
profiles:
  release:
    linkage: wasm
    editor: false
)yaml");
  FakeWasmBackend backend;
  const modules::ResolverOptions options{
      .target = portable_target(),
      .profile = "release",
      .aliases = {{.alias = "runtime", .capability = "runtime.package.v1"}},
  };

  auto loaded = compositions::load_portable_project(directory.path() / "mobagen.yaml", options, backend);

  REQUIRE(loaded.ok());
  CHECK(loaded.runtime->product().name == "portable-project-test");
  CHECK(loaded.runtime->registry().provider_count() == 1);
  CHECK(loaded.runtime->resolution().lifecycle_order().size() == 1);
  REQUIRE(loaded.runtime->plugin(0) != nullptr);
  CHECK(loaded.runtime->plugin(0)->provider().id == "mobagen.wasm-package");
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Query) == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Start) == 1);
  const auto lockfile = loaded.runtime->lockfile({0, 0, 1});
  REQUIRE(lockfile.ok());
  CHECK(*lockfile.contents
        == "schema: 1\n"
           "sdk: 0.0.1\n"
           "target: "
               + portable_target_name()
               + "\n"
                 "profile: release\n"
                 "permissions: []\n"
                 "configurations: {}\n"
                 "resolved:\n"
                 "  runtime.package.v1:\n"
                 "    provider: mobagen.wasm-package\n"
                 "    version: 1.0.0\n"
                 "    linkage: wasm\n"
                 "dependencies: []\n"
                 "plugins:\n"
                 "  mobagen.wasm-package:\n"
                 "    version: 1.0.0\n"
                 "    abi: 1\n"
                 "    package: \"plugins/reference.plugin\"\n"
                 "    hash: "
               + valid_wasm_hash() + "\n");
  CHECK(loaded.runtime->stop().ok());
}

TEST_CASE("Portable project: input and activation failures never publish a partial runtime") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  FakeWasmBackend backend;
  const modules::ResolverOptions options{
      .target = portable_target(),
      .profile = "release",
      .aliases = {{.alias = "runtime", .capability = "runtime.package.v1"}},
  };

  const auto missing = compositions::load_portable_project(directory.path() / "mobagen.yaml", options, backend);
  CHECK_FALSE(missing.ok());
  REQUIRE_FALSE(missing.issues.empty());
  CHECK(missing.issues.front().code == compositions::PortableProjectIssueCode::ReadManifest);
  CHECK(backend.calls == 0);

  write_text(directory.path() / "mobagen.yaml", "schema: [1\n");
  const auto malformed = compositions::load_portable_project(directory.path() / "mobagen.yaml", options, backend);
  CHECK_FALSE(malformed.ok());
  REQUIRE_FALSE(malformed.issues.empty());
  CHECK(malformed.issues.front().code == compositions::PortableProjectIssueCode::ParseManifest);
  CHECK(backend.calls == 0);

  write_text(directory.path() / "mobagen.yaml", std::string(modules::max_product_manifest_bytes + 1, 'x'));
  const auto oversized = compositions::load_portable_project(directory.path() / "mobagen.yaml", options, backend);
  CHECK_FALSE(oversized.ok());
  REQUIRE_FALSE(oversized.issues.empty());
  CHECK(oversized.issues.front().code == compositions::PortableProjectIssueCode::ReadManifest);
  CHECK(backend.calls == 0);

  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  const auto package = directory.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  write_text(directory.path() / "mobagen.yaml", R"yaml(schema: 1
name: rejected-portable-project
modules:
  runtime:
    use: mobagen.wasm-package
plugins:
  - ./plugins/reference.plugin
profiles:
  release:
    linkage: wasm
    editor: false
)yaml");
  backend.start_statuses = {MOBAGEN_WASM_STATUS_FAILED};

  const auto rejected = compositions::load_portable_project(directory.path() / "mobagen.yaml", options, backend);

  CHECK_FALSE(rejected.ok());
  REQUIRE_FALSE(rejected.issues.empty());
  CHECK(rejected.issues.front().code == compositions::PortableProjectIssueCode::Activation);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Quiesce) == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Stop) == 1);
}

TEST_CASE("Portable project: update writes a canonical lock that frozen mode enforces") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  const auto package = directory.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  const auto binary = package / plugins::portable_wasm_plugin_binary_filename();
  write_binary(binary, valid_wasm_header);
  write_text(directory.path() / "mobagen.yaml", R"yaml(schema: 1
name: portable-lock-test
modules:
  runtime:
    use: mobagen.wasm-package
plugins:
  - ./plugins/reference.plugin
profiles:
  release:
    linkage: wasm
    editor: false
)yaml");
  FakeWasmBackend backend;
  const modules::ResolverOptions options{
      .target = portable_target(),
      .profile = "release",
      .aliases = {{.alias = "runtime", .capability = "runtime.package.v1"}},
  };
  const compositions::PortableProjectLockOptions update_lock{
      .policy = compositions::PortableProjectLockPolicy::Update,
      .sdk_version = {0, 0, 1},
  };

  auto updated = compositions::load_portable_project(directory.path() / "mobagen.yaml", options, backend, {}, update_lock);

  REQUIRE(updated.ok());
  const auto generated = updated.runtime->lockfile({0, 0, 1});
  REQUIRE(generated.ok());
  CHECK(read_text(directory.path() / "mobagen.lock") == *generated.contents);
  CHECK(updated.runtime->stop().ok());
  updated.runtime.reset();

  const compositions::PortableProjectLockOptions frozen_lock{
      .policy = compositions::PortableProjectLockPolicy::Frozen,
      .sdk_version = {0, 0, 1},
  };
  auto frozen = compositions::load_portable_project(directory.path() / "mobagen.yaml", options, backend, {}, frozen_lock);
  REQUIRE(frozen.ok());
  CHECK(frozen.runtime->stop().ok());
  frozen.runtime.reset();

  std::vector changed_binary(valid_wasm_header.begin(), valid_wasm_header.end());
  changed_binary.push_back(std::byte{0x00});
  write_binary(binary, changed_binary);
  const auto changed = compositions::load_portable_project(directory.path() / "mobagen.yaml", options, backend, {}, frozen_lock);
  CHECK_FALSE(changed.ok());
  REQUIRE_FALSE(changed.issues.empty());
  CHECK(changed.issues.front().code == compositions::PortableProjectIssueCode::LockMismatch);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Start) == 2);
}

TEST_CASE("Portable project: lock preview does not activate plugins") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  const auto package = directory.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  write_text(directory.path() / "mobagen.yaml", R"yaml(schema: 1
name: portable-preview-test
modules:
  runtime:
    use: mobagen.wasm-package
plugins:
  - ./plugins/reference.plugin
profiles:
  release:
    linkage: wasm
    editor: false
)yaml");
  FakeWasmBackend backend;
  const modules::ResolverOptions options{
      .target = portable_target(),
      .profile = "release",
      .aliases = {{.alias = "runtime", .capability = "runtime.package.v1"}},
  };

  const auto resolved = compositions::resolve_portable_project_lock(directory.path() / "mobagen.yaml", options, backend, {0, 0, 1});

  REQUIRE(resolved.ok());
  CHECK(resolved.lockfile_path == std::filesystem::weakly_canonical(directory.path()) / "mobagen.lock");
  CHECK(resolved.contents->contains("provider: mobagen.wasm-package"));
  CHECK(resolved.preview->product.name == "portable-preview-test");
  CHECK(resolved.preview->registry.provider_count() == 1);
  CHECK(resolved.preview->resolution.registry_generation() == resolved.preview->registry.generation());
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Query) == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Start) == 0);
  CHECK_FALSE(std::filesystem::exists(resolved.lockfile_path));
}

TEST_CASE("Portable project: failed lock update rolls back activated plugins") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  const auto package = directory.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  write_text(directory.path() / "mobagen.yaml", R"yaml(schema: 1
name: portable-lock-write-failure
modules:
  runtime:
    use: mobagen.wasm-package
plugins:
  - ./plugins/reference.plugin
profiles:
  release:
    linkage: wasm
    editor: false
)yaml");
  REQUIRE(std::filesystem::create_directory(directory.path() / "mobagen.lock"));
  FakeWasmBackend backend;
  const modules::ResolverOptions options{
      .target = portable_target(),
      .profile = "release",
      .aliases = {{.alias = "runtime", .capability = "runtime.package.v1"}},
  };
  const compositions::PortableProjectLockOptions update_lock{
      .policy = compositions::PortableProjectLockPolicy::Update,
      .sdk_version = {0, 0, 1},
  };

  const auto failed = compositions::load_portable_project(directory.path() / "mobagen.yaml", options, backend, {}, update_lock);

  CHECK_FALSE(failed.ok());
  REQUIRE_FALSE(failed.issues.empty());
  CHECK(failed.issues.front().code == compositions::PortableProjectIssueCode::LockWrite);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Start) == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Quiesce) == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Stop) == 1);
}
