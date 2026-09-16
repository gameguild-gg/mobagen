#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "plugins/plugin_loader.hpp"

namespace {

  class TemporaryPluginDirectory {
  public:
    TemporaryPluginDirectory() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_
          = std::filesystem::temp_directory_path() / ("mobagen-plugin-loader-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryPluginDirectory() {
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

TEST_CASE("Plugin loader: a real C plugin is loaded through its canonical entry point") {
  using namespace mobagen::plugins;
  const auto host = host_api();
  auto result = load_native_plugin_binary(MOBAGEN_REFERENCE_PLUGIN_PATH, host);

  REQUIRE(result.plugin.has_value());
  CHECK(result.issues.empty());
  CHECK(result.plugin->loaded());
  CHECK(result.plugin->path() == std::filesystem::path{MOBAGEN_REFERENCE_PLUGIN_PATH});
  CHECK(result.plugin->contract().provider.id == "mobagen.reference");
  CHECK(result.plugin->contract().provider.provides == std::vector<std::string>{"runtime.tick.v1"});
}

TEST_CASE("Plugin loader: missing library and entry point return structured failures") {
  using namespace mobagen::plugins;
  const auto host = host_api();
  const auto missing = load_native_plugin_binary("definitely-missing-plugin-binary", host);
  CHECK_FALSE(missing.plugin.has_value());
  CHECK(has_issue(missing, NativePluginLoadIssueCode::open_failed));

  const auto no_entry = load_native_plugin_binary(MOBAGEN_MISSING_ENTRY_PLUGIN_PATH, host);
  CHECK_FALSE(no_entry.plugin.has_value());
  CHECK(has_issue(no_entry, NativePluginLoadIssueCode::missing_entry_point));
}

TEST_CASE("Plugin loader: RAII destroys plugin state and releases the binary") {
  using namespace mobagen::plugins;
  TemporaryPluginDirectory directory;
  const auto source = std::filesystem::path{MOBAGEN_REFERENCE_PLUGIN_PATH};
  const auto copy = directory.path() / source.filename();
  REQUIRE(std::filesystem::copy_file(source, copy));

  {
    const auto host = host_api();
    auto result = load_native_plugin_binary(copy, host);
    REQUIRE(result.plugin.has_value());
  }

  std::error_code error;
  CHECK(std::filesystem::remove(copy, error));
  CHECK_FALSE(error);
}
