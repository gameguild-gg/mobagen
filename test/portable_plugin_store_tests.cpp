#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "plugins/wasm_plugin_store.hpp"
#include "support/wasm_plugin_test_support.hpp"

namespace {

  class TemporaryPortablePluginStoreRoot {
  public:
    TemporaryPortablePluginStoreRoot() { REQUIRE(std::filesystem::create_directory(directory_.path() / "sources")); }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return directory_.path(); }
    [[nodiscard]] std::filesystem::path store() const { return directory_.path() / "installed"; }

    [[nodiscard]] std::filesystem::path package(std::string_view name) const {
      const auto package = directory_.path() / "sources" / (std::string{name} + ".plugin");
      REQUIRE(std::filesystem::create_directory(package));
      mobagen::test::write_binary(package / mobagen::plugins::portable_wasm_plugin_binary_filename(), mobagen::test::valid_wasm_header);
      return package;
    }

  private:
    mobagen::test::TemporaryWasmDirectory directory_;
  };

  bool has_issue(const mobagen::plugins::PortableWasmPluginStoreActionResult& result, mobagen::plugins::PortableWasmPluginStoreIssueCode code) {
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
  }

  bool has_transaction_residue(const std::filesystem::path& store) {
    if (!std::filesystem::exists(store)) return false;
    return std::ranges::any_of(std::filesystem::directory_iterator{store}, [](const auto& entry) {
      const auto filename = entry.path().filename().string();
      return filename.starts_with('.') && entry.path().extension() == ".plugin";
    });
  }

}  // namespace

TEST_CASE("Portable plugin store: validated installation is staged and loadable") {
  using namespace mobagen::plugins;
  TemporaryPortablePluginStoreRoot root;
  const auto source = root.package("reference");
  mobagen::test::FakeWasmBackend backend;
  PortableWasmPluginStore store{root.store()};

  const auto installed = store.install(source, backend);

  REQUIRE(installed.ok());
  CHECK(installed.provider_id == "mobagen.wasm-package");
  CHECK(installed.version == mobagen::modules::SemanticVersion{1, 0, 0});
  CHECK(installed.package == std::filesystem::absolute(root.store() / "mobagen.wasm-package.plugin"));
  const auto loaded = load_portable_wasm_plugin_package(installed.package, backend);
  REQUIRE(loaded.plugin.has_value());
  CHECK(loaded.plugin->provider().id == "mobagen.wasm-package");
  CHECK_FALSE(has_transaction_residue(root.store()));
}

TEST_CASE("Portable plugin store: invalid replacement preserves the installed package") {
  using namespace mobagen::plugins;
  TemporaryPortablePluginStoreRoot root;
  const auto source = root.package("reference");
  mobagen::test::FakeWasmBackend backend;
  PortableWasmPluginStore store{root.store()};
  REQUIRE(store.install(source, backend).ok());
  const auto invalid = root.package("invalid");
  std::ofstream(invalid / "extra.txt") << "forbidden";

  const auto replacement = store.install(invalid, backend);

  CHECK_FALSE(replacement.ok());
  CHECK(has_issue(replacement, PortableWasmPluginStoreIssueCode::InvalidSource));
  const auto loaded = load_portable_wasm_plugin_package(root.store() / "mobagen.wasm-package.plugin", backend);
  REQUIRE(loaded.plugin.has_value());
  CHECK_FALSE(has_transaction_residue(root.store()));
}

TEST_CASE("Portable plugin store: staged identity changes abort before publication") {
  using namespace mobagen::plugins;
  TemporaryPortablePluginStoreRoot root;
  const auto source = root.package("identity-change");
  mobagen::test::FakeWasmBackend backend;
  backend.provider_ids = {"mobagen.source", "mobagen.changed"};
  PortableWasmPluginStore store{root.store()};

  const auto installed = store.install(source, backend);

  CHECK_FALSE(installed.ok());
  CHECK(has_issue(installed, PortableWasmPluginStoreIssueCode::StageFailed));
  CHECK_FALSE(std::filesystem::exists(root.store() / "mobagen.source.plugin"));
  CHECK_FALSE(has_transaction_residue(root.store()));
}

TEST_CASE("Portable plugin store: reinstall replaces atomically and removal leaves no active package") {
  using namespace mobagen::plugins;
  TemporaryPortablePluginStoreRoot root;
  const auto first = root.package("first");
  const auto second = root.package("second");
  mobagen::test::FakeWasmBackend backend;
  PortableWasmPluginStore store{root.store()};
  REQUIRE(store.install(first, backend).ok());
  REQUIRE(store.install(second, backend).ok());

  const auto removed = store.remove("mobagen.wasm-package");

  REQUIRE(removed.ok());
  CHECK(removed.changed);
  CHECK_FALSE(std::filesystem::exists(root.store() / "mobagen.wasm-package.plugin"));
  CHECK_FALSE(has_transaction_residue(root.store()));
}

TEST_CASE("Portable plugin store: invalid provider IDs and missing packages fail safely") {
  using namespace mobagen::plugins;
  TemporaryPortablePluginStoreRoot root;
  REQUIRE(std::filesystem::create_directory(root.store()));
  const auto sentinel = root.path() / "sentinel.txt";
  std::ofstream(sentinel) << "preserve";
  PortableWasmPluginStore store{root.store()};

  const auto invalid = store.remove("../sentinel");
  CHECK_FALSE(invalid.ok());
  CHECK(has_issue(invalid, PortableWasmPluginStoreIssueCode::InvalidProvider));
  CHECK(std::filesystem::exists(sentinel));

  const auto missing = store.remove("mobagen.missing");
  CHECK_FALSE(missing.ok());
  CHECK(has_issue(missing, PortableWasmPluginStoreIssueCode::NotFound));
}

TEST_CASE("Portable plugin store: files cannot impersonate roots or installed packages") {
  using namespace mobagen::plugins;
  TemporaryPortablePluginStoreRoot root;
  const auto source = root.package("reference");
  mobagen::test::FakeWasmBackend backend;

  const auto root_file = root.path() / "store-file";
  std::ofstream(root_file) << "preserve";
  const auto invalid_root = PortableWasmPluginStore{root_file}.install(source, backend);
  CHECK_FALSE(invalid_root.ok());
  CHECK(has_issue(invalid_root, PortableWasmPluginStoreIssueCode::InvalidRoot));

  REQUIRE(std::filesystem::create_directory(root.store()));
  const auto destination_file = root.store() / "mobagen.wasm-package.plugin";
  std::ofstream(destination_file) << "preserve";
  const auto invalid_destination = PortableWasmPluginStore{root.store()}.install(source, backend);
  CHECK_FALSE(invalid_destination.ok());
  CHECK(has_issue(invalid_destination, PortableWasmPluginStoreIssueCode::CommitFailed));
  CHECK(std::filesystem::is_regular_file(destination_file));
}

TEST_CASE("Portable plugin store: inventory validates and deterministically reports packages") {
  using namespace mobagen::plugins;
  TemporaryPortablePluginStoreRoot root;
  const auto source = root.package("reference");
  mobagen::test::FakeWasmBackend install_backend;
  PortableWasmPluginStore store{root.store()};
  REQUIRE(store.install(source, install_backend).ok());
  const auto alpha = root.store() / "mobagen.alpha.plugin";
  REQUIRE(std::filesystem::create_directory(alpha));
  mobagen::test::write_binary(alpha / portable_wasm_plugin_binary_filename(), mobagen::test::valid_wasm_header);
  mobagen::test::FakeWasmBackend list_backend;
  list_backend.provider_ids = {"mobagen.alpha", "mobagen.wasm-package"};
  list_backend.capability_ids = {"runtime.alpha.v1", "runtime.package.v1"};

  const auto inventory = store.list(list_backend);

  REQUIRE(inventory.ok());
  REQUIRE(inventory.entries.size() == 2);
  CHECK(inventory.entries[0].provider.id == "mobagen.alpha");
  CHECK(inventory.entries[0].package == std::filesystem::absolute(alpha));
  CHECK(inventory.entries[1].provider.id == "mobagen.wasm-package");
  CHECK(inventory.entries[1].provider.linkages == std::vector{mobagen::modules::LinkageMode::Wasm});
  CHECK(inventory.entries[1].package == std::filesystem::absolute(root.store() / "mobagen.wasm-package.plugin"));
}

TEST_CASE("Portable plugin store: inventory reports corrupt dot-plugin packages") {
  using namespace mobagen::plugins;
  TemporaryPortablePluginStoreRoot root;
  REQUIRE(std::filesystem::create_directories(root.store() / "corrupt.plugin"));
  std::ofstream(root.store() / "corrupt.plugin" / "unexpected.txt") << "not a plugin";
  mobagen::test::FakeWasmBackend backend;

  const auto inventory = PortableWasmPluginStore{root.store()}.list(backend);

  CHECK_FALSE(inventory.ok());
  CHECK(inventory.entries.empty());
  REQUIRE(inventory.issues.size() == 1);
  CHECK(inventory.issues.front().code == PortableWasmPluginStoreIssueCode::InvalidSource);
  CHECK(inventory.issues.front().path == std::filesystem::absolute(root.store() / "corrupt.plugin"));
}
