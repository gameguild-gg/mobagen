#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

#include "plugin_cli.hpp"
#include "plugins/plugin_loader.hpp"

namespace {

  class TemporaryPluginCliRoot {
  public:
    TemporaryPluginCliRoot() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path() / ("mobagen-plugin-cli-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directories(path_));
    }

    ~TemporaryPluginCliRoot() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] std::filesystem::path package() const {
      const auto package = path_ / "source.plugin";
      REQUIRE(std::filesystem::create_directory(package));
      REQUIRE(std::filesystem::copy_file(MOBAGEN_REFERENCE_PLUGIN_PATH, package / mobagen::plugins::native_plugin_binary_filename()));
      return package;
    }

    [[nodiscard]] std::filesystem::path store() const { return path_ / "installed"; }

  private:
    std::filesystem::path path_;
  };

}  // namespace

TEST_CASE("Plugin CLI: verify reports native package identity") {
  TemporaryPluginCliRoot root;
  const auto package = root.package().string();
  const std::array<std::string_view, 2> arguments{"verify", package};
  std::ostringstream output;
  std::ostringstream error;

  CHECK(mobagen::plugins::cli::run(arguments, output, error) == 0);
  CHECK(error.str().empty());
  CHECK(output.str() == "verified\tmobagen.reference\t1.0.0\n");
}

TEST_CASE("Plugin CLI: install and remove round-trip through the managed store") {
  TemporaryPluginCliRoot root;
  const auto package = root.package().string();
  const auto store = root.store().string();
  const std::array<std::string_view, 3> install_arguments{"install", store, package};
  std::ostringstream install_output;
  std::ostringstream install_error;

  REQUIRE(mobagen::plugins::cli::run(install_arguments, install_output, install_error) == 0);
  CHECK(install_error.str().empty());
  CHECK(install_output.str().starts_with("installed\tmobagen.reference\t1.0.0\t"));
  CHECK(std::filesystem::is_directory(root.store() / "mobagen.reference.plugin"));

  const std::array<std::string_view, 2> list_arguments{"list", store};
  std::ostringstream list_output;
  std::ostringstream list_error;
  CHECK(mobagen::plugins::cli::run(list_arguments, list_output, list_error) == 0);
  CHECK(list_error.str().empty());
  CHECK(list_output.str().starts_with("plugin\tmobagen.reference\t1.0.0\t"));
  CHECK(list_output.str().ends_with("mobagen.reference.plugin\nplugins\t1\n"));

  const std::array<std::string_view, 3> remove_arguments{"remove", store, "mobagen.reference"};
  std::ostringstream remove_output;
  std::ostringstream remove_error;
  CHECK(mobagen::plugins::cli::run(remove_arguments, remove_output, remove_error) == 0);
  CHECK(remove_error.str().empty());
  CHECK(remove_output.str().starts_with("removed\tmobagen.reference\t"));
  CHECK_FALSE(std::filesystem::exists(root.store() / "mobagen.reference.plugin"));
}

TEST_CASE("Plugin CLI: malformed commands and invalid packages fail explicitly") {
  std::ostringstream output;
  std::ostringstream error;
  const std::array<std::string_view, 1> unknown{"unknown"};
  CHECK(mobagen::plugins::cli::run(unknown, output, error) == 2);
  CHECK(error.str().contains("usage:"));

  output.str({});
  error.str({});
  const std::array<std::string_view, 2> invalid{"verify", "missing.plugin"};
  CHECK(mobagen::plugins::cli::run(invalid, output, error) == 3);
  CHECK(error.str().contains("verify failed"));
}
