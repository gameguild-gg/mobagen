#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "asset_cli.hpp"

namespace {

  class TemporaryCliDirectory {
  public:
    TemporaryCliDirectory() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path() / ("mobagen-assets-cli-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryCliDirectory() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
  };

  void write_text(const std::filesystem::path& path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    REQUIRE(stream.good());
  }

}  // namespace

TEST_CASE("Asset CLI: scan lists regular files in deterministic relative order") {
  TemporaryCliDirectory directory;
  write_text(directory.path() / "z-last.bin", "z");
  write_text(directory.path() / "nested" / "a-first.bin", "abc");
  const auto root = directory.path().string();
  const std::array<std::string_view, 2> arguments{"scan", root};
  std::ostringstream output;
  std::ostringstream error;

  CHECK(mobagen::assets::cli::run(arguments, output, error) == 0);
  CHECK(error.str().empty());
  CHECK(output.str() == "file\t3\tnested/a-first.bin\nfile\t1\tz-last.bin\nfiles\t2\n");
}

TEST_CASE("Asset CLI: import and verify round-trip through the native cache") {
  TemporaryCliDirectory directory;
  const auto source = directory.path() / "source.bin";
  const auto cache = directory.path() / "cache";
  write_text(source, "asset payload");
  const auto source_text = source.string();
  const auto cache_text = cache.string();
  const std::array<std::string_view, 3> import_arguments{"import", cache_text, source_text};
  std::ostringstream import_output;
  std::ostringstream import_error;

  REQUIRE(mobagen::assets::cli::run(import_arguments, import_output, import_error) == 0);
  CHECK(import_error.str().empty());
  const auto line = import_output.str();
  REQUIRE(line.starts_with("imported\tsha256:"));
  REQUIRE(line.ends_with('\n'));
  const auto id = line.substr(std::string_view{"imported\t"}.size(), line.size() - std::string_view{"imported\t"}.size() - 1);

  const std::array<std::string_view, 3> verify_arguments{"verify", cache_text, id};
  std::ostringstream verify_output;
  std::ostringstream verify_error;
  CHECK(mobagen::assets::cli::run(verify_arguments, verify_output, verify_error) == 0);
  CHECK(verify_error.str().empty());
  CHECK(verify_output.str() == "verified\t" + id + "\t13\n");
}

TEST_CASE("Asset CLI: malformed commands and identifiers fail explicitly") {
  TemporaryCliDirectory directory;
  const auto cache = directory.path().string();
  std::ostringstream output;
  std::ostringstream error;
  const std::array<std::string_view, 1> unknown{"unknown"};
  CHECK(mobagen::assets::cli::run(unknown, output, error) == 2);
  CHECK(error.str().contains("usage:"));

  output.str({});
  error.str({});
  const std::array<std::string_view, 3> invalid_id{"verify", cache, "sha256:not-an-id"};
  CHECK(mobagen::assets::cli::run(invalid_id, output, error) == 2);
  CHECK(error.str().contains("invalid asset id"));
}
