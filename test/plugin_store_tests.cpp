#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "plugins/plugin_store.hpp"

namespace {

  class TemporaryPluginStoreRoot {
  public:
    TemporaryPluginStoreRoot() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_
          = std::filesystem::temp_directory_path() / ("mobagen-plugin-store-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directories(path_ / "sources"));
    }

    ~TemporaryPluginStoreRoot() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::filesystem::path store() const { return path_ / "installed"; }

    [[nodiscard]] std::filesystem::path package(std::string_view name) const {
      const auto package = path_ / "sources" / (std::string{name} + ".plugin");
      REQUIRE(std::filesystem::create_directory(package));
      REQUIRE(std::filesystem::copy_file(MOBAGEN_REFERENCE_PLUGIN_PATH, package / mobagen::plugins::native_plugin_binary_filename()));
      return package;
    }

  private:
    std::filesystem::path path_;
  };

  bool has_issue(const mobagen::plugins::NativePluginStoreActionResult& result, mobagen::plugins::NativePluginStoreIssueCode code) {
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
  }

  bool has_transaction_residue(const std::filesystem::path& store) {
    if (!std::filesystem::exists(store)) {
      return false;
    }
    return std::ranges::any_of(std::filesystem::directory_iterator{store}, [](const auto& entry) {
      const auto filename = entry.path().filename().string();
      return filename.starts_with('.') && entry.path().extension() == ".plugin";
    });
  }

}  // namespace

TEST_CASE("Plugin store: validated package installation is staged and loadable") {
  using namespace mobagen::plugins;
  TemporaryPluginStoreRoot root;
  const auto source = root.package("reference");
  PluginHost host;
  NativePluginStore store{root.store()};

  const auto installed = store.install(source, host);

  REQUIRE(installed.ok());
  CHECK(installed.provider_id == "mobagen.reference");
  CHECK(installed.version == mobagen::modules::SemanticVersion{1, 0, 0});
  CHECK(installed.package == std::filesystem::absolute(root.store() / "mobagen.reference.plugin"));
  const auto loaded = load_native_plugin_package(installed.package, host.api());
  REQUIRE(loaded.plugin.has_value());
  CHECK(loaded.plugin->contract().provider.id == "mobagen.reference");
  CHECK_FALSE(has_transaction_residue(root.store()));
}

TEST_CASE("Plugin store: invalid replacement preserves the installed package") {
  using namespace mobagen::plugins;
  TemporaryPluginStoreRoot root;
  const auto source = root.package("reference");
  PluginHost host;
  NativePluginStore store{root.store()};
  REQUIRE(store.install(source, host).ok());
  const auto invalid = root.package("invalid");
  std::ofstream(invalid / "extra.txt") << "forbidden";

  const auto replacement = store.install(invalid, host);

  CHECK_FALSE(replacement.ok());
  CHECK(has_issue(replacement, NativePluginStoreIssueCode::InvalidSource));
  const auto loaded = load_native_plugin_package(root.store() / "mobagen.reference.plugin", host.api());
  REQUIRE(loaded.plugin.has_value());
  CHECK_FALSE(has_transaction_residue(root.store()));
}

TEST_CASE("Plugin store: reinstall replaces atomically and removal leaves no active package") {
  using namespace mobagen::plugins;
  TemporaryPluginStoreRoot root;
  const auto first = root.package("first");
  const auto second = root.package("second");
  PluginHost host;
  NativePluginStore store{root.store()};
  REQUIRE(store.install(first, host).ok());
  REQUIRE(store.install(second, host).ok());

  const auto removed = store.remove("mobagen.reference");

  REQUIRE(removed.ok());
  CHECK(removed.changed);
  CHECK_FALSE(std::filesystem::exists(root.store() / "mobagen.reference.plugin"));
  CHECK_FALSE(has_transaction_residue(root.store()));
}

TEST_CASE("Plugin store: invalid provider IDs and missing packages return explicit safe failures") {
  using namespace mobagen::plugins;
  TemporaryPluginStoreRoot root;
  REQUIRE(std::filesystem::create_directories(root.store()));
  const auto sentinel = root.path() / "sentinel.txt";
  std::ofstream(sentinel) << "preserve";
  NativePluginStore store{root.store()};

  const auto invalid = store.remove("../sentinel");
  CHECK_FALSE(invalid.ok());
  CHECK(has_issue(invalid, NativePluginStoreIssueCode::InvalidProvider));
  CHECK(std::filesystem::exists(sentinel));

  const auto missing = store.remove("mobagen.missing");
  CHECK_FALSE(missing.ok());
  CHECK(has_issue(missing, NativePluginStoreIssueCode::NotFound));
}

TEST_CASE("Plugin store: files cannot impersonate the store root or an installed package") {
  using namespace mobagen::plugins;
  TemporaryPluginStoreRoot root;
  const auto source = root.package("reference");
  PluginHost host;

  const auto root_file = root.path() / "store-file";
  std::ofstream(root_file) << "preserve";
  const auto invalid_root = NativePluginStore{root_file}.install(source, host);
  CHECK_FALSE(invalid_root.ok());
  CHECK(has_issue(invalid_root, NativePluginStoreIssueCode::InvalidRoot));

  REQUIRE(std::filesystem::create_directories(root.store()));
  const auto destination_file = root.store() / "mobagen.reference.plugin";
  std::ofstream(destination_file) << "preserve";
  const auto invalid_destination = NativePluginStore{root.store()}.install(source, host);
  CHECK_FALSE(invalid_destination.ok());
  CHECK(has_issue(invalid_destination, NativePluginStoreIssueCode::CommitFailed));
  CHECK(std::filesystem::is_regular_file(destination_file));
}

TEST_CASE("Plugin store: inventory validates and deterministically reports installed packages") {
  using namespace mobagen::plugins;
  TemporaryPluginStoreRoot root;
  const auto source = root.package("reference");
  PluginHost host;
  NativePluginStore store{root.store()};
  REQUIRE(store.install(source, host).ok());
  const auto lifecycle_package = root.store() / "mobagen.lifecycle-failure.plugin";
  REQUIRE(std::filesystem::create_directory(lifecycle_package));
  REQUIRE(std::filesystem::copy_file(MOBAGEN_CONFIGURE_FAILURE_PLUGIN_PATH, lifecycle_package / native_plugin_binary_filename()));

  const auto inventory = store.list(host);

  REQUIRE(inventory.ok());
  REQUIRE(inventory.entries.size() == 2);
  CHECK(inventory.entries[0].provider_id == "mobagen.lifecycle-failure");
  CHECK(inventory.entries[0].package == std::filesystem::absolute(lifecycle_package));
  CHECK(inventory.entries[1].provider_id == "mobagen.reference");
  CHECK(inventory.entries[1].version == mobagen::modules::SemanticVersion{1, 0, 0});
  CHECK(inventory.entries[1].package == std::filesystem::absolute(root.store() / "mobagen.reference.plugin"));
}

TEST_CASE("Plugin store: inventory reports a corrupt dot-plugin package") {
  using namespace mobagen::plugins;
  TemporaryPluginStoreRoot root;
  REQUIRE(std::filesystem::create_directories(root.store() / "corrupt.plugin"));
  std::ofstream(root.store() / "corrupt.plugin" / "unexpected.txt") << "not a plugin";
  PluginHost host;

  const auto inventory = NativePluginStore{root.store()}.list(host);

  CHECK_FALSE(inventory.ok());
  CHECK(inventory.entries.empty());
  REQUIRE(inventory.issues.size() == 1);
  CHECK(inventory.issues.front().code == NativePluginStoreIssueCode::InvalidSource);
  CHECK(inventory.issues.front().path == std::filesystem::absolute(root.store() / "corrupt.plugin"));
}
