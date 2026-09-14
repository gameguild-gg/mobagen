#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "plugins/plugin_loader.hpp"

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

  void* MOBAGEN_PLUGIN_CALL allocate(void*, std::size_t, std::size_t) { return nullptr; }
  void MOBAGEN_PLUGIN_CALL deallocate(void*, void*, std::size_t, std::size_t) {}
  void MOBAGEN_PLUGIN_CALL log(void*, MobagenLogLevel, MobagenStringView) {}
  MobagenStatus MOBAGEN_PLUGIN_CALL publish(void*, MobagenStringView, std::uint32_t, const void*, std::uint32_t) { return MOBAGEN_STATUS_OK; }
  MobagenStatus MOBAGEN_PLUGIN_CALL find(void*, MobagenStringView, std::uint32_t, const void**, std::uint32_t*) { return MOBAGEN_STATUS_NOT_FOUND; }

  MobagenHostApiV1 host_api() {
    return {
        .struct_size = MOBAGEN_PLUGIN_HOST_API_V1_SIZE,
        .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
        .host_context = nullptr,
        .allocate = allocate,
        .deallocate = deallocate,
        .log = log,
        .publish_capability = publish,
        .find_capability = find,
    };
  }

  bool has_issue(const mobagen::plugins::NativePluginLoadResult& result, mobagen::plugins::NativePluginLoadIssueCode code) {
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
  }

}  // namespace

TEST_CASE("Plugin package: a dot-plugin directory loads its canonical native binary") {
  using namespace mobagen::plugins;
  TemporaryPackageDirectory directory;
  const auto package = directory.path() / "reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  const auto binary = package / native_plugin_binary_filename();
  REQUIRE(std::filesystem::copy_file(MOBAGEN_REFERENCE_PLUGIN_PATH, binary));

  const auto host = host_api();
  auto result = load_native_plugin_package(package, host);
  REQUIRE(result.plugin.has_value());
  CHECK(result.issues.empty());
  CHECK(result.plugin->path() == std::filesystem::absolute(binary));
  CHECK(result.plugin->contract().provider.id == "mobagen.reference");
}

TEST_CASE("Plugin package: extension directory and binary shape are strict") {
  using namespace mobagen::plugins;
  TemporaryPackageDirectory directory;
  const auto host = host_api();

  const auto wrong_extension = directory.path() / "reference.bundle";
  REQUIRE(std::filesystem::create_directory(wrong_extension));
  const auto extension_result = load_native_plugin_package(wrong_extension, host);
  CHECK_FALSE(extension_result.plugin.has_value());
  CHECK(has_issue(extension_result, NativePluginLoadIssueCode::invalid_package));

  const auto missing_binary = directory.path() / "missing.plugin";
  REQUIRE(std::filesystem::create_directory(missing_binary));
  const auto missing_result = load_native_plugin_package(missing_binary, host);
  CHECK_FALSE(missing_result.plugin.has_value());
  CHECK(has_issue(missing_result, NativePluginLoadIssueCode::missing_package_binary));

  const auto regular_file = directory.path() / "file.plugin";
  std::ofstream(regular_file) << "not a package";
  const auto file_result = load_native_plugin_package(regular_file, host);
  CHECK_FALSE(file_result.plugin.has_value());
  CHECK(has_issue(file_result, NativePluginLoadIssueCode::invalid_package));

  const auto extra_files = directory.path() / "extra.plugin";
  REQUIRE(std::filesystem::create_directory(extra_files));
  REQUIRE(std::filesystem::copy_file(MOBAGEN_REFERENCE_PLUGIN_PATH, extra_files / native_plugin_binary_filename()));
  std::ofstream(extra_files / "README.txt") << "not part of the package";
  const auto extra_result = load_native_plugin_package(extra_files, host);
  CHECK_FALSE(extra_result.plugin.has_value());
  CHECK(has_issue(extra_result, NativePluginLoadIssueCode::invalid_package));
}
