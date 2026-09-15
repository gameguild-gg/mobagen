#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "assets/asset_cache.hpp"
#include "http/client.hpp"
#include "modules/artifact_installer.hpp"
#include "plugins/plugin_loader.hpp"
#include "project_bootstrap.hpp"
#include "project_cli.hpp"
#include "project_startup.hpp"
#include "support/wasm_plugin_test_support.hpp"
#include <mobagen/version.h>

namespace {

  class TemporaryProjectCliRoot {
  public:
    TemporaryProjectCliRoot() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path() / ("mobagen-project-cli-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directories(path_ / "plugins" / "reference.plugin"));
      REQUIRE(std::filesystem::copy_file(MOBAGEN_REFERENCE_PLUGIN_PATH,
                                         path_ / "plugins" / "reference.plugin" / mobagen::plugins::native_plugin_binary_filename()));
      REQUIRE(std::filesystem::create_directories(path_ / "plugins" / "alternative.plugin"));
      REQUIRE(std::filesystem::copy_file(MOBAGEN_CONFIGURE_FAILURE_PLUGIN_PATH,
                                         path_ / "plugins" / "alternative.plugin" / mobagen::plugins::native_plugin_binary_filename()));
      std::ofstream manifest(path_ / "mobagen.yaml", std::ios::binary);
      REQUIRE(manifest.is_open());
      manifest << R"yaml(schema: 1
name: project-cli-test
modules:
  runtime:
    capability: runtime.tick.v1
    use: default
    config:
      schema: mobagen.reference.config.v1
      data: "41"
plugins:
  - ./plugins/reference.plugin
  - ./plugins/alternative.plugin
profiles:
  release:
    linkage: dynamic
    editor: false
    permissions:
      - debug
)yaml";
      REQUIRE(manifest.good());
    }

    ~TemporaryProjectCliRoot() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
  };

  std::vector<std::string_view> project_arguments(std::string_view command, const std::string& manifest) {
    return {command, manifest, "--profile", "release", "--alias", "runtime=runtime.tick.v1", "--default", "runtime.tick.v1=mobagen.reference"};
  }

  class ProjectCatalogHttpClient final : public mobagen::http::Client {
  public:
    ProjectCatalogHttpClient() {
      constexpr std::string_view artifact = "remote-native-plugin";
      artifact_body.resize(artifact.size());
      for (std::size_t index = 0; index < artifact.size(); ++index) {
        artifact_body[index] = static_cast<std::byte>(artifact[index]);
      }
      const auto id = mobagen::assets::sha256(artifact_body);
      REQUIRE(id.has_value());
      artifact_hash = mobagen::assets::to_string(*id);
    }

    mobagen::http::GetResult get(const mobagen::http::GetRequest& request) override {
      catalog_requests.push_back(request);
      std::ostringstream catalog;
      catalog << "schema: 1\n"
                 "providers:\n"
                 "  mobagen.runtime.remote:\n"
                 "    version: 2.1.0\n"
                 "    provides: [runtime.tick.v1]\n"
                 "    reload: restart\n"
                 "    artifacts:\n";
      for (const auto platform : {"windows", "linux", "macos"}) {
        catalog << "      - target: " << platform << '\n'
                << "        linkage: dynamic\n"
                << "        abi: 1\n"
                << "        url: https://plugins.mobagen.dev/mobagen.runtime.remote/2.1.0/" << platform << ".plugin\n"
                << "        size: " << artifact_body.size() << '\n'
                << "        hash: " << artifact_hash << '\n';
      }
      const auto text = std::move(catalog).str();
      std::vector<std::byte> body(text.size());
      for (std::size_t index = 0; index < text.size(); ++index) {
        body[index] = static_cast<std::byte>(text[index]);
      }
      return {.response = mobagen::http::Response{200, std::move(body)}};
    }

    mobagen::http::StreamGetResult get_stream(const mobagen::http::GetRequest& request, mobagen::http::BodySink sink) override {
      artifact_requests.push_back(request);
      if (!sink.write(sink.context, artifact_body)) {
        return {.error = mobagen::http::Error{mobagen::http::ErrorCode::SinkRejected, "artifact cache rejected test bytes"}};
      }
      return {.response = mobagen::http::StreamResponse{200, artifact_body.size()}};
    }

    std::vector<std::byte> artifact_body;
    std::string artifact_hash;
    std::vector<mobagen::http::GetRequest> catalog_requests;
    std::vector<mobagen::http::GetRequest> artifact_requests;
  };

  constexpr std::string_view native_artifact_filename() noexcept {
#ifdef _WIN32
    return "windows.plugin";
#elif defined(__APPLE__)
    return "macos.plugin";
#else
    return "linux.plugin";
#endif
  }

  mobagen::modules::TargetPlatform project_target() noexcept {
#ifdef _WIN32
    return mobagen::modules::TargetPlatform::Windows;
#elif defined(__APPLE__)
    return mobagen::modules::TargetPlatform::MacOS;
#else
    return mobagen::modules::TargetPlatform::Linux;
#endif
  }

  void write_remote_project_manifest(const std::filesystem::path& path, std::string_view trailing = {}) {
    std::ofstream manifest_file(path, std::ios::binary | std::ios::trunc);
    REQUIRE(manifest_file.is_open());
    manifest_file << R"yaml(schema: 1
name: remote-project-cli-test
sources:
  official:
    url: https://plugins.mobagen.dev/v1/catalog.yaml
modules:
  runtime:
    capability: runtime.tick.v1
    use: default
plugins:
  - ./plugins/missing.plugin
profiles:
  release:
    linkage: dynamic
    editor: false
)yaml" << trailing;
    REQUIRE(manifest_file.good());
  }

  std::vector<std::string_view> remote_project_arguments(std::string_view command, const std::string& manifest) {
    return {
        command, manifest, "--profile", "release", "--default", "runtime.tick.v1=mobagen.runtime.remote",
    };
  }

}  // namespace

TEST_CASE("Project CLI: init writes a self-contained recommended module template") {
  TemporaryProjectCliRoot temporary;
  const auto project_path = temporary.path() / "recommended-project";
  const auto project = project_path.string();
  const std::vector<std::string_view> arguments{
      "init", project, "--name", "sample-game", "--source", "https://registry.example/mobagen/catalog.yaml",
  };
  std::ostringstream output;
  std::ostringstream error;

  REQUIRE(mobagen::compositions::cli::run(arguments, output, error, {}) == 0);
  CHECK(error.str().empty());
  const auto manifest_path = project_path / "mobagen.yaml";
  REQUIRE(std::filesystem::is_regular_file(manifest_path));
  const auto manifest = mobagen::test::read_text(manifest_path);
  CHECK(manifest == R"yaml(schema: 1
name: sample-game
sources:
  official:
    url: "https://registry.example/mobagen/catalog.yaml"
modules:
  assets:
    capability: assets.store.v1
    use: mobagen.assets.default
  render:
    capability: render.backend.v1
    use: mobagen.render.webgpu
  window:
    capability: window.surface.v1
    use: mobagen.window.sdl3
plugins: []
profiles:
  development:
    linkage: dynamic
    editor: true
    permissions:
      - filesystem-read
      - gpu
      - windowing
  release:
    linkage: dynamic
    editor: false
    permissions:
      - filesystem-read
      - gpu
      - windowing
)yaml");
  CHECK(output.str().contains("initialized\t" + manifest_path.generic_string() + '\n'));
  CHECK(output.str().contains("template\trecommended\n"));
  CHECK(output.str().contains("next\tMobagenProject bootstrap\t"));

  output.str({});
  CHECK(mobagen::compositions::cli::run(arguments, output, error, {}) == 3);
  CHECK(error.str().contains("already exists"));
  CHECK(mobagen::test::read_text(manifest_path) == manifest);
}

TEST_CASE("Project CLI: sync downloads a selected plugin once without loading a local package") {
  TemporaryProjectCliRoot project;
  write_remote_project_manifest(project.path() / "mobagen.yaml");
  const auto manifest = (project.path() / "mobagen.yaml").string();
  const auto arguments = remote_project_arguments("sync", manifest);
  ProjectCatalogHttpClient client;
  std::ostringstream output;
  std::ostringstream error;

  const auto result = mobagen::compositions::cli::run(arguments, output, error, {.http_client = &client});

  INFO(error.str());
  REQUIRE(result == 0);
  CHECK(error.str().empty());
  REQUIRE(client.catalog_requests.size() == 1);
  CHECK(client.catalog_requests.front().url == "https://plugins.mobagen.dev/v1/catalog.yaml");
  REQUIRE(client.artifact_requests.size() == 1);
  CHECK(output.str().contains("catalogs-synced\tremote-project-cli-test\trelease\n"));
  const std::string expected_artifact = "artifact\tmobagen.runtime.remote\t2.1.0\tdynamic\t1\t" + std::to_string(client.artifact_body.size()) + '\t'
                                        + client.artifact_hash + "\thttps://plugins.mobagen.dev/mobagen.runtime.remote/2.1.0/"
                                        + std::string{native_artifact_filename()} + '\n';
  CHECK(output.str().contains(expected_artifact));
  CHECK(output.str().contains("cache\tmobagen.runtime.remote\tdownloaded\t"));
  CHECK(output.str().contains("plugin\tmobagen.runtime.remote\tinstalled\t"));
  CHECK(output.str().contains("lock\t"));
  CHECK(output.str().ends_with("selected\t1\n"));
  const auto id = mobagen::assets::parse_asset_id(client.artifact_hash);
  REQUIRE(id.has_value());
  mobagen::assets::AssetCache cache(project.path() / ".mobagen" / "cache");
  CHECK(std::filesystem::is_regular_file(cache.path_for(*id)));
  const auto package = project.path() / ".mobagen" / "plugins" / "mobagen.runtime.remote.plugin";
  CHECK(std::filesystem::is_regular_file(package / mobagen::modules::module_plugin_binary_filename(mobagen::modules::LinkageMode::Dynamic)));
  const auto lockfile = mobagen::test::read_text(project.path() / "mobagen.lock");
  CHECK(lockfile.contains("  mobagen.runtime.remote:\n"));
  CHECK(lockfile.contains("    version: 2.1.0\n"));
  CHECK(lockfile.contains("    abi: 1\n"));
  CHECK(lockfile.contains("manifest: sha256:"));
  CHECK(lockfile.contains("    package: \".mobagen/plugins/mobagen.runtime.remote.plugin\"\n"));
  CHECK(lockfile.contains("    hash: " + client.artifact_hash + '\n'));

  output.str({});
  error.str({});
  const auto repeated_result = mobagen::compositions::cli::run(arguments, output, error, {.http_client = &client});
  INFO(error.str());
  REQUIRE(repeated_result == 0);
  CHECK(error.str().empty());
  CHECK(client.catalog_requests.size() == 2);
  CHECK(client.artifact_requests.size() == 1);
  CHECK(output.str().contains("cache\tmobagen.runtime.remote\tpresent\t"));
  CHECK(output.str().contains("plugin\tmobagen.runtime.remote\tpresent\t"));
  CHECK(mobagen::test::read_text(project.path() / "mobagen.lock") == lockfile);
}

TEST_CASE("Project CLI: bootstrap downloads once then validates the locked project without HTTP") {
  TemporaryProjectCliRoot project;
  write_remote_project_manifest(project.path() / "mobagen.yaml");
  const auto manifest = (project.path() / "mobagen.yaml").string();
  const auto arguments = remote_project_arguments("bootstrap", manifest);
  ProjectCatalogHttpClient client;
  std::ostringstream output;
  std::ostringstream error;

  REQUIRE(mobagen::compositions::cli::run(arguments, output, error, {.http_client = &client}) == 0);
  CHECK(error.str().empty());
  CHECK(client.catalog_requests.size() == 1);
  CHECK(client.artifact_requests.size() == 1);

  output.str({});
  REQUIRE(mobagen::compositions::cli::run(arguments, output, error, {.http_client = &client}) == 0);
  CHECK(error.str().empty());
  CHECK(output.str().contains("ready\tremote-project-cli-test\trelease\n"));
  CHECK(output.str().contains("plugins\t1\n"));
  CHECK(client.catalog_requests.size() == 1);
  CHECK(client.artifact_requests.size() == 1);

  output.str({});
  REQUIRE(mobagen::compositions::cli::run(arguments, output, error, {}) == 0);
  CHECK(output.str().contains("ready\tremote-project-cli-test\trelease\n"));
  CHECK(error.str().empty());

  const auto package = project.path() / ".mobagen" / "plugins" / "mobagen.runtime.remote.plugin";
  const auto binary = package / mobagen::modules::module_plugin_binary_filename(mobagen::modules::LinkageMode::Dynamic);
  std::ofstream(binary, std::ios::binary | std::ios::app) << "tampered";
  output.str({});
  REQUIRE(mobagen::compositions::cli::run(arguments, output, error, {.http_client = &client}) == 0);
  CHECK(error.str().empty());
  CHECK(client.catalog_requests.size() == 2);
  CHECK(client.artifact_requests.size() == 1);
  CHECK(output.str().contains("plugin\tmobagen.runtime.remote\tinstalled\t"));

  write_remote_project_manifest(project.path() / "mobagen.yaml", "# changed\n");
  output.str({});
  REQUIRE(mobagen::compositions::cli::run(arguments, output, error, {.http_client = &client}) == 0);
  CHECK(error.str().empty());
  CHECK(client.catalog_requests.size() == 3);
  CHECK(client.artifact_requests.size() == 1);
}

TEST_CASE("Project bootstrap: first run prepares modules and later lazy probes stay offline") {
  using namespace mobagen;
  TemporaryProjectCliRoot project;
  write_remote_project_manifest(project.path() / "mobagen.yaml");
  ProjectCatalogHttpClient client;
  compositions::ProjectBootstrapOptions options{
      .resolver = {
          .target = project_target(),
          .profile = "release",
          .defaults = {{
              .target = project_target(),
              .profile = "release",
              .capability = "runtime.tick.v1",
              .provider = "mobagen.runtime.remote",
          }},
      },
      .sdk_version = {
          MOBAGEN_SDK_VERSION_MAJOR, MOBAGEN_SDK_VERSION_MINOR,
          MOBAGEN_SDK_VERSION_PATCH,
      },
  };

  const auto prepared = compositions::bootstrap_project(project.path() / "mobagen.yaml", options, &client);

  REQUIRE(prepared.ok());
  CHECK(prepared.state == compositions::ProjectBootstrapState::Synchronized);
  CHECK(prepared.product_name == "remote-project-cli-test");
  CHECK(prepared.plugin_count == 1);
  CHECK(client.catalog_requests.size() == 1);
  CHECK(client.artifact_requests.size() == 1);

  const auto binary = project.path() / ".mobagen" / "plugins" / "mobagen.runtime.remote.plugin"
                      / modules::module_plugin_binary_filename(modules::LinkageMode::Dynamic);
  std::ofstream(binary, std::ios::binary | std::ios::app) << "tampered";

  const auto offline = compositions::bootstrap_project(project.path() / "mobagen.yaml", options);

  REQUIRE(offline.ok());
  CHECK(offline.state == compositions::ProjectBootstrapState::Ready);
  CHECK(offline.plugin_count == 1);
  CHECK(client.catalog_requests.size() == 1);
  CHECK(client.artifact_requests.size() == 1);
}

TEST_CASE("Project startup: first run downloads then opens a cold module manager") {
  using namespace mobagen;
  TemporaryProjectCliRoot project;
  write_remote_project_manifest(project.path() / "mobagen.yaml");
  ProjectCatalogHttpClient client;
  compositions::ProjectBootstrapOptions options{
      .resolver = {
          .target = project_target(),
          .profile = "release",
          .defaults = {{
              .target = project_target(),
              .profile = "release",
              .capability = "runtime.tick.v1",
              .provider = "mobagen.runtime.remote",
          }},
      },
      .sdk_version = {
          MOBAGEN_SDK_VERSION_MAJOR, MOBAGEN_SDK_VERSION_MINOR,
          MOBAGEN_SDK_VERSION_PATCH,
      },
  };

  auto started = compositions::prepare_and_open_project(project.path() / "mobagen.yaml", options, {.http_client = &client});

  REQUIRE(started.ok());
  CHECK(started.bootstrap.state == compositions::ProjectBootstrapState::Synchronized);
  CHECK(started.project.manager->kind() == compositions::ProjectModuleRuntimeKind::Native);
  CHECK(started.project.manager->active_count() == 0);
  CHECK(client.catalog_requests.size() == 1);
  CHECK(client.artifact_requests.size() == 1);
  CHECK(started.project.manager->stop().ok());
  started.project.manager.reset();

  auto reopened = compositions::prepare_and_open_project(project.path() / "mobagen.yaml", std::move(options));

  REQUIRE(reopened.ok());
  CHECK(reopened.bootstrap.state == compositions::ProjectBootstrapState::Ready);
  CHECK(reopened.project.manager->active_count() == 0);
  CHECK(client.catalog_requests.size() == 1);
  CHECK(client.artifact_requests.size() == 1);
  CHECK(reopened.project.manager->stop().ok());
}

TEST_CASE("Project CLI: sync reports a missing injected HTTPS service without network access") {
  TemporaryProjectCliRoot project;
  const auto manifest = (project.path() / "mobagen.yaml").string();
  const auto arguments = project_arguments("sync", manifest);
  std::ostringstream output;
  std::ostringstream error;

  CHECK(mobagen::compositions::cli::run(arguments, output, error, {}) == 3);
  CHECK(output.str().empty());
  CHECK(error.str().contains("HTTPS client is unavailable"));
}

TEST_CASE("Project CLI: resolve writes a canonical lock and verify accepts it") {
  TemporaryProjectCliRoot project;
  const auto manifest = (project.path() / "mobagen.yaml").string();
  std::ostringstream output;
  std::ostringstream error;

  const auto resolve_arguments = project_arguments("resolve", manifest);
  REQUIRE(mobagen::compositions::cli::run(resolve_arguments, output, error) == 0);
  CHECK(error.str().empty());
  CHECK(output.str().starts_with("resolved\t"));
  CHECK(std::filesystem::is_regular_file(project.path() / "mobagen.lock"));
  std::ifstream lockfile(project.path() / "mobagen.lock", std::ios::binary);
  const std::string lock_contents{std::istreambuf_iterator<char>{lockfile}, std::istreambuf_iterator<char>{}};
  CHECK(lock_contents.contains("sdk: " MOBAGEN_SDK_VERSION_STRING "\n"));
  CHECK(lock_contents.contains("permissions:\n  - debug\n"));
  CHECK(
      lock_contents.contains("configurations:\n  mobagen.reference:\n    schema: mobagen.reference.config.v1\n"
                             "    hash: sha256:3d914f9348c9cc0ff8a79716700b9fcd4d2f3e711608004eb8f138bcba7f14d9\n"));

  output.str({});
  const auto verify_arguments = project_arguments("verify", manifest);
  CHECK(mobagen::compositions::cli::run(verify_arguments, output, error) == 0);
  CHECK(error.str().empty());
  CHECK(output.str().starts_with("verified\t"));

  std::ofstream(project.path() / "mobagen.lock", std::ios::binary | std::ios::app) << "# changed\n";
  output.str({});
  CHECK(mobagen::compositions::cli::run(verify_arguments, output, error) == 3);
  CHECK(error.str().contains("differs from the resolved project"));
}

TEST_CASE("Project CLI: malformed options fail with usage without touching a lock") {
  TemporaryProjectCliRoot project;
  const auto manifest = (project.path() / "mobagen.yaml").string();
  const std::vector<std::string_view> arguments{"resolve", manifest, "--alias", "missing-separator"};
  std::ostringstream output;
  std::ostringstream error;

  CHECK(mobagen::compositions::cli::run(arguments, output, error) == 2);
  CHECK(error.str().contains("usage:"));
  CHECK_FALSE(std::filesystem::exists(project.path() / "mobagen.lock"));
}

TEST_CASE("Project CLI: explain reports selected and available providers without writing a lock") {
  TemporaryProjectCliRoot project;
  const auto manifest = (project.path() / "mobagen.yaml").string();
  const auto arguments = project_arguments("explain", manifest);
  std::ostringstream output;
  std::ostringstream error;

  REQUIRE(mobagen::compositions::cli::run(arguments, output, error) == 0);
  CHECK(error.str().empty());
  CHECK(output.str().contains("project\tproject-cli-test\n"));
  CHECK(output.str().contains("profile\trelease\n"));
  CHECK(output.str().contains("grant\trelease\tdebug\n"));
  CHECK(output.str().contains("provider\tmobagen.lifecycle-failure\t1.0.0\tavailable\n"));
  CHECK(output.str().contains("provides\tmobagen.lifecycle-failure\truntime.tick.v1\n"));
  CHECK(output.str().contains("provider\tmobagen.reference\t1.0.0\tselected\n"));
  CHECK(output.str().contains("config-schema\tmobagen.reference\tmobagen.reference.config.v1\n"));
  CHECK(output.str().contains("permission\tmobagen.reference\tdebug\n"));
  CHECK(output.str().contains("configuration\tmobagen.reference\tmobagen.reference.config.v1\t2\n"));
  CHECK(output.str().contains("selection\truntime.tick.v1\tmobagen.reference\tdynamic\tdefault for profile 'release'\n"));
  CHECK(output.str().contains("providers\t2\nselections\t1\ndependencies\t0\nconfigurations\t1\n"));
  CHECK_FALSE(output.str().contains("configuration-data"));
  CHECK_FALSE(std::filesystem::exists(project.path() / "mobagen.lock"));
}

TEST_CASE("Project CLI: a wasm profile resolves through the injected portable backend") {
  using namespace mobagen;
  using namespace mobagen::test;
  TemporaryWasmDirectory project;
  REQUIRE(std::filesystem::create_directory(project.path() / "plugins"));
  const auto package = project.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  write_text(project.path() / "mobagen.yaml", R"yaml(schema: 1
name: portable-project-cli-test
modules:
  runtime:
    use: default
plugins:
  - ./plugins/reference.plugin
profiles:
  release:
    linkage: wasm
    editor: false
)yaml");
  FakeWasmBackend backend;
  const auto manifest = (project.path() / "mobagen.yaml").string();
  const std::vector<std::string_view> arguments{
      "resolve", manifest, "--profile", "release", "--alias", "runtime=runtime.package.v1", "--default", "runtime.package.v1=mobagen.wasm-package"};
  std::ostringstream output;
  std::ostringstream error;

  REQUIRE(compositions::cli::run(arguments, output, error, {.portable_backend = &backend}) == 0);
  CHECK(error.str().empty());
  CHECK(output.str().starts_with("resolved\t"));
  CHECK(backend.calls == 1);
  CHECK(read_text(project.path() / "mobagen.lock").contains("linkage: wasm\n"));

  output.str({});
  const std::vector<std::string_view> verify_arguments{
      "verify", manifest, "--profile", "release", "--alias", "runtime=runtime.package.v1", "--default", "runtime.package.v1=mobagen.wasm-package"};
  REQUIRE(compositions::cli::run(verify_arguments, output, error, {.portable_backend = &backend}) == 0);
  CHECK(error.str().empty());
  CHECK(output.str().starts_with("verified\t"));
  CHECK(backend.calls == 2);

  output.str({});
  const std::vector<std::string_view> explain_arguments{
      "explain", manifest, "--profile", "release", "--alias", "runtime=runtime.package.v1", "--default", "runtime.package.v1=mobagen.wasm-package"};
  REQUIRE(compositions::cli::run(explain_arguments, output, error, {.portable_backend = &backend}) == 0);
  CHECK(error.str().empty());
  CHECK(output.str().contains("selection\truntime.package.v1\tmobagen.wasm-package\twasm\t"));
  CHECK(backend.calls == 3);
}

TEST_CASE("Project CLI: a wasm profile fails clearly when no portable backend is available") {
  using namespace mobagen;
  using namespace mobagen::test;
  TemporaryWasmDirectory project;
  write_text(project.path() / "mobagen.yaml", R"yaml(schema: 1
name: unavailable-portable-project-cli-test
modules: {}
plugins: []
profiles:
  release:
    linkage: wasm
    editor: false
)yaml");
  const auto manifest = (project.path() / "mobagen.yaml").string();
  const auto arguments = project_arguments("resolve", manifest);
  std::ostringstream output;
  std::ostringstream error;

  CHECK(compositions::cli::run(arguments, output, error, {}) == 3);
  CHECK(output.str().empty());
  CHECK(error.str().contains("portable WASM backend is unavailable"));
  CHECK_FALSE(std::filesystem::exists(project.path() / "mobagen.lock"));
}
