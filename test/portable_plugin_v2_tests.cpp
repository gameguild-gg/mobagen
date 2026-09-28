#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "modules/module_manifest.hpp"
#include "plugin_cli.hpp"
#include "plugins/wasm_plugin_store.hpp"
#include "support/wasm_plugin_test_support.hpp"

namespace {

  using namespace mobagen;
  using plugins::portable_wasm_plugin_aot_filename;
  using plugins::portable_wasm_plugin_binary_filename;
  using plugins::portable_wasm_plugin_manifest_filename;

  constexpr std::array aot_payload{std::byte{0x00}, std::byte{'a'}, std::byte{'o'}, std::byte{'t'}, std::byte{0x01}};

  class TemporaryV2Root {
  public:
    TemporaryV2Root() { REQUIRE(std::filesystem::create_directories(path_ / "sources")); }

    ~TemporaryV2Root() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::filesystem::path store() const { return path_ / "installed"; }
    [[nodiscard]] std::filesystem::path sources() const { return path_ / "sources"; }

  private:
    std::filesystem::path path_ = test::TemporaryWasmDirectory{}.path();
  };

  std::string hash_bytes(std::span<const std::byte> bytes) {
    const auto digest = assets::sha256(bytes);
    REQUIRE(digest.has_value());
    return assets::to_string(*digest);
  }

  /* A full .plugin v2 package: plugin.wasm + plugin.aot + module.manifest whose
   * payloads describe both files with correct SHA-256 hashes and sizes. */
  std::filesystem::path write_v2_package(const std::filesystem::path& package, std::string wasm_hash_override = {}) {
    REQUIRE(std::filesystem::create_directory(package));
    test::write_binary(package / portable_wasm_plugin_binary_filename(), test::valid_wasm_header);
    test::write_binary(package / portable_wasm_plugin_aot_filename(), aot_payload);

    modules::ModuleManifest manifest;
    manifest.api_version = 1;
    manifest.abi_version = 1;
    manifest.entry = std::string{modules::module_entry_symbol_v1};
    manifest.payloads.push_back({std::string{modules::module_wasm_payload_filename},
                                 wasm_hash_override.empty() ? hash_bytes(test::valid_wasm_header) : std::move(wasm_hash_override),
                                 test::valid_wasm_header.size()});
    manifest.payloads.push_back({std::string{modules::module_aot_payload_filename}, hash_bytes(aot_payload), aot_payload.size()});
    test::write_text(package / portable_wasm_plugin_manifest_filename(), modules::serialize_module_manifest(manifest));
    return package;
  }

  bool has_issue(const plugins::PortableWasmPluginStoreActionResult& result, plugins::PortableWasmPluginStoreIssueCode code) {
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
  }

  bool has_transaction_residue(const std::filesystem::path& store) {
    if (!std::filesystem::exists(store)) return false;
    return std::ranges::any_of(std::filesystem::directory_iterator{store}, [](const auto& entry) {
      const auto filename = entry.path().filename().string();
      return filename.starts_with('.') && entry.path().extension() == ".plugin";
    });
  }

  bool same_bytes(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::ifstream left_input{left, std::ios::binary};
    std::ifstream right_input{right, std::ios::binary};
    return std::equal(std::istreambuf_iterator<char>{left_input}, std::istreambuf_iterator<char>{},
                      std::istreambuf_iterator<char>{right_input}, std::istreambuf_iterator<char>{});
  }

}  // namespace

TEST_CASE("Portable plugin store: a v2 package installs with its manifest and AOT payload intact") {
  TemporaryV2Root root;
  const auto source = write_v2_package(root.sources() / "source.plugin");
  test::FakeWasmBackend backend;
  plugins::PortableWasmPluginStore store{root.store()};

  const auto installed = store.install(source, backend);

  REQUIRE(installed.ok());
  CHECK(installed.provider_id == "mobagen.wasm-package");
  const auto package = root.store() / "mobagen.wasm-package.plugin";
  CHECK(std::filesystem::is_regular_file(package / portable_wasm_plugin_binary_filename()));
  CHECK(std::filesystem::is_regular_file(package / portable_wasm_plugin_aot_filename()));
  CHECK(std::filesystem::is_regular_file(package / portable_wasm_plugin_manifest_filename()));
  CHECK(same_bytes(package / portable_wasm_plugin_binary_filename(), source / portable_wasm_plugin_binary_filename()));
  CHECK(same_bytes(package / portable_wasm_plugin_aot_filename(), source / portable_wasm_plugin_aot_filename()));
  CHECK(same_bytes(package / portable_wasm_plugin_manifest_filename(), source / portable_wasm_plugin_manifest_filename()));
  const auto loaded = plugins::load_portable_wasm_plugin_package(package, backend);
  REQUIRE(loaded.plugin.has_value());
  CHECK(loaded.plugin->provider().id == "mobagen.wasm-package");
  CHECK_FALSE(has_transaction_residue(root.store()));
}

TEST_CASE("Portable plugin store: a v2 package with a tampered payload hash fails install") {
  TemporaryV2Root root;
  const std::string wrong_hash = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  const auto source = write_v2_package(root.sources() / "tampered.plugin", wrong_hash);
  test::FakeWasmBackend backend;
  plugins::PortableWasmPluginStore store{root.store()};

  const auto installed = store.install(source, backend);

  CHECK_FALSE(installed.ok());
  CHECK(has_issue(installed, plugins::PortableWasmPluginStoreIssueCode::StageFailed));
  REQUIRE(!installed.issues.empty());
  CHECK(installed.issues.front().message.contains("plugin.wasm"));
  CHECK_FALSE(std::filesystem::exists(root.store() / "mobagen.wasm-package.plugin"));
  CHECK_FALSE(has_transaction_residue(root.store()));
}

TEST_CASE("Portable plugin store: a v2 manifest listing a payload the package lacks fails install") {
  TemporaryV2Root root;
  const auto source = write_v2_package(root.sources() / "missing-aot.plugin");
  REQUIRE(std::filesystem::remove(source / portable_wasm_plugin_aot_filename()));
  test::FakeWasmBackend backend;
  plugins::PortableWasmPluginStore store{root.store()};

  const auto installed = store.install(source, backend);

  CHECK_FALSE(installed.ok());
  CHECK(has_issue(installed, plugins::PortableWasmPluginStoreIssueCode::StageFailed));
  REQUIRE(!installed.issues.empty());
  CHECK(installed.issues.front().message.contains("plugin.aot"));
  CHECK_FALSE(has_transaction_residue(root.store()));
}

TEST_CASE("Plugin CLI: a v2 package verify, install, and list round-trip") {
  TemporaryV2Root root;
  const auto source = write_v2_package(root.sources() / "source.plugin");
  const std::string source_text = source.string();
  const std::string store = root.store().string();
  test::FakeWasmBackend backend;
  const plugins::cli::PluginCliServices services{.portable_backend = &backend};

  std::ostringstream output;
  std::ostringstream error;
  const std::array<std::string_view, 2> verify_arguments{"verify", source_text};
  const int verify_status = plugins::cli::run(verify_arguments, output, error, services);
  if (verify_status != 0) std::fprintf(stderr, "verify failed: %s\n", error.str().c_str());
  REQUIRE(verify_status == 0);
  CHECK(error.str().empty());
  CHECK(output.str() == "verified\tmobagen.wasm-package\t1.0.0\n");

  output.str({});
  const std::array<std::string_view, 3> install_arguments{"install", store, source_text};
  REQUIRE(plugins::cli::run(install_arguments, output, error, services) == 0);
  CHECK(error.str().empty());
  CHECK(output.str().starts_with("installed\tmobagen.wasm-package\t1.0.0\t"));
  const auto package = root.store() / "mobagen.wasm-package.plugin";
  CHECK(std::filesystem::is_regular_file(package / portable_wasm_plugin_binary_filename()));
  CHECK(std::filesystem::is_regular_file(package / portable_wasm_plugin_aot_filename()));
  CHECK(std::filesystem::is_regular_file(package / portable_wasm_plugin_manifest_filename()));

  output.str({});
  const std::array<std::string_view, 2> list_arguments{"list", store};
  REQUIRE(plugins::cli::run(list_arguments, output, error, services) == 0);
  CHECK(error.str().empty());
  CHECK(output.str().contains("plugin\tmobagen.wasm-package\t1.0.0\twasm\t"));
  CHECK(output.str().ends_with("plugins\t1\n"));
}

TEST_CASE("Plugin CLI: installing a tampered v2 package fails through the CLI") {
  TemporaryV2Root root;
  const std::string wrong_hash = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  const auto source = write_v2_package(root.sources() / "tampered.plugin", wrong_hash);
  const std::string source_text = source.string();
  const std::string store = root.store().string();
  test::FakeWasmBackend backend;
  const plugins::cli::PluginCliServices services{.portable_backend = &backend};

  std::ostringstream output;
  std::ostringstream error;
  const std::array<std::string_view, 3> install_arguments{"install", store, source_text};
  CHECK(plugins::cli::run(install_arguments, output, error, services) == 3);
  CHECK(output.str().empty());
  CHECK(error.str().contains("install failed"));
  CHECK(error.str().contains("plugin.wasm"));
  CHECK_FALSE(std::filesystem::exists(root.store() / "mobagen.wasm-package.plugin"));
}
