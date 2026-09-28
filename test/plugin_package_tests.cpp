#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

#include "plugins/plugin_package.hpp"
#include "plugins/wasm_plugin_loader.hpp"

namespace {

  class TemporaryPackageDirectory {
  public:
    TemporaryPackageDirectory() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path()
              / ("mobagen-plugin-package-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryPackageDirectory() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
  };

  void write_wasm_binary(const std::filesystem::path& destination) {
    const std::array wasm_header{
        std::byte{0x00}, std::byte{0x61}, std::byte{0x73}, std::byte{0x6d}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };
    std::ofstream stream(destination, std::ios::binary);
    REQUIRE(stream.is_open());
    stream.write(reinterpret_cast<const char*>(wasm_header.data()), static_cast<std::streamsize>(wasm_header.size()));
  }

}  // namespace

TEST_CASE("Plugin package: canonical contents classify portable wasm packages without loading them") {
  using namespace mobagen::plugins;
  TemporaryPackageDirectory directory;

  const auto portable_package = directory.path() / "portable.plugin";
  REQUIRE(std::filesystem::create_directory(portable_package));
  write_wasm_binary(portable_package / portable_wasm_plugin_binary_filename());
  const auto portable = inspect_plugin_package(portable_package);
  REQUIRE(portable.ok());
  CHECK(*portable.kind == PluginPackageKind::PortableWasm);

  std::ofstream(portable_package / "unexpected.txt") << "ambiguous package";
  const auto ambiguous = inspect_plugin_package(portable_package);
  CHECK_FALSE(ambiguous.ok());
  REQUIRE(ambiguous.issue.has_value());
  CHECK(ambiguous.issue->code == PluginPackageInspectionIssueCode::InvalidContents);
}

TEST_CASE("Plugin package: native-shaped packages are rejected loudly") {
  using namespace mobagen::plugins;
  TemporaryPackageDirectory directory;

  const auto extension_wrong = directory.path() / "native.bundle";
  REQUIRE(std::filesystem::create_directory(extension_wrong));
  const auto wrong_extension = inspect_plugin_package(extension_wrong);
  CHECK_FALSE(wrong_extension.ok());
  REQUIRE(wrong_extension.issue.has_value());
  CHECK(wrong_extension.issue->code == PluginPackageInspectionIssueCode::InvalidPath);

  const auto native_package = directory.path() / "native.plugin";
  REQUIRE(std::filesystem::create_directory(native_package));
  std::ofstream(native_package / "plugin.dylib") << "native binary";
  const auto native = inspect_plugin_package(native_package);
  CHECK_FALSE(native.ok());
  REQUIRE(native.issue.has_value());
  CHECK(native.issue->code == PluginPackageInspectionIssueCode::InvalidContents);
  CHECK(native.issue->message.find("native plugin tier was removed") != std::string::npos);

  const auto empty_package = directory.path() / "empty.plugin";
  REQUIRE(std::filesystem::create_directory(empty_package));
  const auto empty = inspect_plugin_package(empty_package);
  CHECK_FALSE(empty.ok());
  REQUIRE(empty.issue.has_value());
  CHECK(empty.issue->code == PluginPackageInspectionIssueCode::InvalidContents);

  const auto regular_file = directory.path() / "file.plugin";
  std::ofstream(regular_file) << "not a package";
  const auto file_result = inspect_plugin_package(regular_file);
  CHECK_FALSE(file_result.ok());
  REQUIRE(file_result.issue.has_value());
  CHECK(file_result.issue->code == PluginPackageInspectionIssueCode::InvalidPath);
}
