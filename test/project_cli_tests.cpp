#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "plugins/plugin_loader.hpp"
#include "project_cli.hpp"

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
      std::ofstream manifest(path_ / "mobagen.yaml", std::ios::binary);
      REQUIRE(manifest.is_open());
      manifest << R"yaml(schema: 1
name: project-cli-test
modules:
  runtime:
    use: default
plugins:
  - ./plugins/reference.plugin
profiles:
  release:
    linkage: dynamic
    editor: false
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
    return {command, manifest, "--profile", "release", "--alias", "runtime=runtime.tick.v1", "--default", "runtime.tick.v1=mobagen.reference",
            "--sdk", "0.0.1"};
  }

}  // namespace

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
