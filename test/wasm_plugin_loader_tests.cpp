#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "plugins/wasm_plugin_catalog.hpp"

namespace {

  constexpr std::array valid_wasm_header{
      std::byte{0x00}, std::byte{0x61}, std::byte{0x73}, std::byte{0x6d}, std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
  };

  class TemporaryWasmDirectory {
  public:
    TemporaryWasmDirectory() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path() / ("mobagen-wasm-loader-" + std::to_string(ticks) + "-" + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryWasmDirectory() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
  };

  void write_binary(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.is_open());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
  }

  void write_u32(std::vector<std::byte>& memory, std::size_t offset, std::uint32_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
      memory[offset + byte] = std::byte{static_cast<std::uint8_t>(value >> (byte * 8U))};
    }
  }

  void write_string(std::vector<std::byte>& memory, std::uint32_t offset, std::string_view value) {
    std::ranges::transform(value, memory.begin() + offset, [](char byte) { return std::byte{static_cast<std::uint8_t>(byte)}; });
  }

  class DescriptorInstance final : public mobagen::plugins::PortableWasmInstance {
  public:
    explicit DescriptorInstance(std::shared_ptr<std::vector<mobagen::plugins::WasmPluginExport>> invocations,
                                std::string provider_id = "mobagen.wasm-package")
        : invocations_(std::move(invocations)), provider_id_(std::move(provider_id)) {}

    mobagen::plugins::WasmInvocationResult invoke(mobagen::plugins::WasmPluginExport function, std::span<const std::uint32_t> arguments) override {
      invocations_->push_back(function);
      if (function == mobagen::plugins::WasmPluginExport::Allocate) {
        return mobagen::plugins::WasmInvocationResult::success(8);
      }
      if (function == mobagen::plugins::WasmPluginExport::Query) {
        encode_descriptor(arguments[0]);
        return mobagen::plugins::WasmInvocationResult::success(MOBAGEN_WASM_STATUS_OK);
      }
      if (function == mobagen::plugins::WasmPluginExport::Deallocate) {
        return mobagen::plugins::WasmInvocationResult::success(MOBAGEN_WASM_STATUS_OK);
      }
      if (function == mobagen::plugins::WasmPluginExport::Configure || function == mobagen::plugins::WasmPluginExport::Start
          || function == mobagen::plugins::WasmPluginExport::Quiesce || function == mobagen::plugins::WasmPluginExport::Stop) {
        return mobagen::plugins::WasmInvocationResult::success(MOBAGEN_WASM_STATUS_OK);
      }
      return mobagen::plugins::WasmInvocationResult::failure("unexpected export");
    }

    std::span<const std::byte> memory() const noexcept override { return linear_memory; }
    std::span<std::byte> writable_memory() noexcept override { return linear_memory; }

    bool malformed{};

  private:
    void encode_descriptor(std::uint32_t descriptor_offset) {
      constexpr std::uint32_t id_offset = 96;
      constexpr std::uint32_t capability_offset = 128;
      constexpr std::uint32_t provides_offset = 152;
      constexpr std::string_view capability = "runtime.package.v1";
      write_string(linear_memory, id_offset, provider_id_);
      write_string(linear_memory, capability_offset, capability);
      write_u32(linear_memory, provides_offset, capability_offset);
      write_u32(linear_memory, provides_offset + 4, static_cast<std::uint32_t>(capability.size()));
      write_u32(linear_memory, descriptor_offset, malformed ? 0 : MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
      write_u32(linear_memory, descriptor_offset + 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
      write_u32(linear_memory, descriptor_offset + 8, id_offset);
      write_u32(linear_memory, descriptor_offset + 12, static_cast<std::uint32_t>(provider_id_.size()));
      write_u32(linear_memory, descriptor_offset + 16, 1);
      write_u32(linear_memory, descriptor_offset + 20, 0);
      write_u32(linear_memory, descriptor_offset + 24, 0);
      write_u32(linear_memory, descriptor_offset + 28, MOBAGEN_WASM_RELOAD_RESTART);
      write_u32(linear_memory, descriptor_offset + 32, provides_offset);
      write_u32(linear_memory, descriptor_offset + 36, 1);
      for (std::uint32_t field = 40; field < MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE; field += 4) {
        write_u32(linear_memory, descriptor_offset + field, 0);
      }
    }

    std::shared_ptr<std::vector<mobagen::plugins::WasmPluginExport>> invocations_;
    std::string provider_id_;
    std::vector<std::byte> linear_memory = std::vector<std::byte>(256);
  };

  class FakeWasmBackend final : public mobagen::plugins::PortableWasmBackend {
  public:
    mobagen::plugins::PortableWasmInstantiationResult instantiate(std::span<const std::byte> binary) override {
      ++calls;
      observed.assign(binary.begin(), binary.end());
      if (throws) throw std::runtime_error{"backend trapped"};
      if (fails) return mobagen::plugins::PortableWasmInstantiationResult::failure("backend rejected module");
      const auto provider_id = provider_ids.empty() ? std::string{"mobagen.wasm-package"} : provider_ids.at(calls - 1);
      auto instance = std::make_unique<DescriptorInstance>(invocations, provider_id);
      instance->malformed = malformed_descriptor;
      return mobagen::plugins::PortableWasmInstantiationResult::success(std::move(instance));
    }

    std::vector<std::byte> observed;
    std::shared_ptr<std::vector<mobagen::plugins::WasmPluginExport>> invocations
        = std::make_shared<std::vector<mobagen::plugins::WasmPluginExport>>();
    std::size_t calls{};
    std::vector<std::string> provider_ids;
    bool fails{};
    bool throws{};
    bool malformed_descriptor{};
  };

  bool has_issue(const mobagen::plugins::PortableWasmPluginLoadResult& result, mobagen::plugins::PortableWasmPluginLoadIssueCode code) {
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
  }

  bool has_issue(const mobagen::plugins::PortableWasmPluginCatalogResult& result, mobagen::plugins::PortableWasmPluginCatalogIssueCode code) {
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
  }

}  // namespace

TEST_CASE("Portable WASM plugin loader: a bounded module is instantiated and queried") {
  TemporaryWasmDirectory directory;
  const auto binary = directory.path() / "reference.wasm";
  write_binary(binary, valid_wasm_header);
  FakeWasmBackend backend;

  auto result = mobagen::plugins::load_portable_wasm_plugin_binary(binary, backend);

  REQUIRE(result.plugin.has_value());
  CHECK(result.issues.empty());
  CHECK(result.plugin->loaded());
  CHECK(result.plugin->path() == std::filesystem::absolute(binary));
  CHECK(result.plugin->provider().id == "mobagen.wasm-package");
  CHECK(result.plugin->provider().linkages == std::vector{mobagen::modules::LinkageMode::Wasm});
  CHECK(backend.calls == 1);
  CHECK(backend.observed == std::vector<std::byte>{valid_wasm_header.begin(), valid_wasm_header.end()});
  CHECK(result.plugin->take_instance() != nullptr);
  CHECK_FALSE(result.plugin->loaded());
}

TEST_CASE("Portable WASM plugin loader: malformed and oversized files fail before the backend") {
  TemporaryWasmDirectory directory;
  FakeWasmBackend backend;

  const auto missing = mobagen::plugins::load_portable_wasm_plugin_binary(directory.path() / "missing.wasm", backend);
  CHECK(has_issue(missing, mobagen::plugins::PortableWasmPluginLoadIssueCode::OpenFailed));

  const auto malformed_path = directory.path() / "malformed.wasm";
  auto malformed_header = valid_wasm_header;
  malformed_header[1] = std::byte{0xff};
  write_binary(malformed_path, malformed_header);
  const auto malformed = mobagen::plugins::load_portable_wasm_plugin_binary(malformed_path, backend);
  CHECK(has_issue(malformed, mobagen::plugins::PortableWasmPluginLoadIssueCode::InvalidBinary));

  const auto unsupported_path = directory.path() / "unsupported.wasm";
  auto unsupported_header = valid_wasm_header;
  unsupported_header[4] = std::byte{0x02};
  write_binary(unsupported_path, unsupported_header);
  const auto unsupported = mobagen::plugins::load_portable_wasm_plugin_binary(unsupported_path, backend);
  CHECK(has_issue(unsupported, mobagen::plugins::PortableWasmPluginLoadIssueCode::UnsupportedVersion));

  const auto oversized_path = directory.path() / "oversized.wasm";
  std::ofstream oversized(oversized_path, std::ios::binary | std::ios::trunc);
  REQUIRE(oversized.is_open());
  oversized.seekp(static_cast<std::streamoff>(mobagen::plugins::max_portable_wasm_plugin_binary_bytes));
  oversized.put('\0');
  oversized.close();
  const auto too_large = mobagen::plugins::load_portable_wasm_plugin_binary(oversized_path, backend);
  CHECK(has_issue(too_large, mobagen::plugins::PortableWasmPluginLoadIssueCode::SizeLimit));
  CHECK(backend.calls == 0);
}

TEST_CASE("Portable WASM plugin loader: backend and descriptor failures remain structured") {
  TemporaryWasmDirectory directory;
  const auto binary = directory.path() / "reference.wasm";
  write_binary(binary, valid_wasm_header);

  FakeWasmBackend rejected;
  rejected.fails = true;
  const auto backend_failure = mobagen::plugins::load_portable_wasm_plugin_binary(binary, rejected);
  CHECK(has_issue(backend_failure, mobagen::plugins::PortableWasmPluginLoadIssueCode::BackendFailure));

  FakeWasmBackend throwing;
  throwing.throws = true;
  const auto backend_trap = mobagen::plugins::load_portable_wasm_plugin_binary(binary, throwing);
  CHECK(has_issue(backend_trap, mobagen::plugins::PortableWasmPluginLoadIssueCode::BackendFailure));

  FakeWasmBackend malformed;
  malformed.malformed_descriptor = true;
  const auto query_failure = mobagen::plugins::load_portable_wasm_plugin_binary(binary, malformed);
  CHECK(has_issue(query_failure, mobagen::plugins::PortableWasmPluginLoadIssueCode::QueryFailed));
  REQUIRE(query_failure.issues.size() == 1);
  CHECK_FALSE(query_failure.issues[0].query_issues.empty());
}

TEST_CASE("Portable WASM plugin loader: queried metadata transfers into activation without a second query") {
  TemporaryWasmDirectory directory;
  const auto binary = directory.path() / "reference.wasm";
  write_binary(binary, valid_wasm_header);
  FakeWasmBackend backend;
  auto loaded = mobagen::plugins::load_portable_wasm_plugin_binary(binary, backend);
  REQUIRE(loaded.plugin.has_value());

  auto activated = mobagen::plugins::activate_loaded_portable_wasm_plugin(std::move(*loaded.plugin));

  REQUIRE(activated.activation != nullptr);
  CHECK(activated.activation->provider().id == "mobagen.wasm-package");
  CHECK(activated.activation->state() == mobagen::plugins::PortableWasmPluginState::Active);
  CHECK(std::ranges::count(*backend.invocations, mobagen::plugins::WasmPluginExport::Query) == 1);
  CHECK(backend.invocations->back() == mobagen::plugins::WasmPluginExport::Start);
  CHECK(activated.activation->quiesce().ok());
  CHECK(activated.activation->stop().ok());
}

TEST_CASE("Portable WASM plugin loader: dot-plugin package shape is strict") {
  TemporaryWasmDirectory directory;
  FakeWasmBackend backend;
  const auto package = directory.path() / "reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / mobagen::plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);

  const auto valid = mobagen::plugins::load_portable_wasm_plugin_package(package, backend);
  REQUIRE(valid.plugin.has_value());
  CHECK(valid.plugin->provider().id == "mobagen.wasm-package");

  std::ofstream(package / "README.txt") << "unexpected";
  const auto extra = mobagen::plugins::load_portable_wasm_plugin_package(package, backend);
  CHECK(has_issue(extra, mobagen::plugins::PortableWasmPluginLoadIssueCode::InvalidPackage));

  const auto wrong_extension = directory.path() / "reference.bundle";
  REQUIRE(std::filesystem::create_directory(wrong_extension));
  const auto wrong = mobagen::plugins::load_portable_wasm_plugin_package(wrong_extension, backend);
  CHECK(has_issue(wrong, mobagen::plugins::PortableWasmPluginLoadIssueCode::InvalidPackage));

  const auto missing_package = directory.path() / "missing.plugin";
  REQUIRE(std::filesystem::create_directory(missing_package));
  const auto missing = mobagen::plugins::load_portable_wasm_plugin_package(missing_package, backend);
  CHECK(has_issue(missing, mobagen::plugins::PortableWasmPluginLoadIssueCode::MissingPackageBinary));
}

TEST_CASE("Portable WASM plugin catalog: manifest packages join builtins in one registry") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  const auto plugin_directory = directory.path() / "plugins";
  REQUIRE(std::filesystem::create_directory(plugin_directory));
  const auto package = plugin_directory / "reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  modules::ProductDescriptor product{.name = "wasm-catalog", .plugins = {"plugins/reference.plugin"}};
  const modules::ProviderDescriptor builtin{
      .id = "mobagen.runtime.builtin",
      .version = {1, 0, 0},
      .provides = {"runtime.builtin.v1"},
      .targets = {
          modules::TargetPlatform::Windows,
          modules::TargetPlatform::Linux,
          modules::TargetPlatform::MacOS,
          modules::TargetPlatform::Web,
          modules::TargetPlatform::Android,
          modules::TargetPlatform::IOS,
      },
      .linkages = {modules::LinkageMode::Static},
  };
  FakeWasmBackend backend;

  auto result = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend, std::span(&builtin, 1));

  REQUIRE(result.ok());
  CHECK(result.catalog->plugin_count() == 1);
  REQUIRE(result.catalog->plugin(0) != nullptr);
  CHECK(result.catalog->plugin(0)->provider().id == "mobagen.wasm-package");
  CHECK(result.catalog->registry().provider_count() == 2);
  CHECK(result.catalog->registry().find_provider("mobagen.wasm-package").has_value());
  auto taken = result.catalog->take_plugin("mobagen.wasm-package");
  REQUIRE(taken.has_value());
  CHECK(taken->loaded());
  CHECK(result.catalog->plugin_count() == 0);
}

TEST_CASE("Portable WASM plugin catalog: paths cannot escape or name one package twice") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  const auto project = directory.path() / "project";
  REQUIRE(std::filesystem::create_directories(project / "plugins"));
  const auto package = project / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  const auto outside = directory.path() / "outside.plugin";
  REQUIRE(std::filesystem::create_directory(outside));
  write_binary(outside / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  FakeWasmBackend backend;

  const modules::ProductDescriptor escaped{.name = "wasm-catalog", .plugins = {"../outside.plugin"}};
  const auto escaped_result = plugins::discover_portable_wasm_plugin_catalog(escaped, project, backend);
  CHECK(has_issue(escaped_result, plugins::PortableWasmPluginCatalogIssueCode::PathOutsideProject));

  const modules::ProductDescriptor duplicate{
      .name = "wasm-catalog",
      .plugins = {"plugins/reference.plugin", "./plugins/../plugins/reference.plugin"},
  };
  const auto duplicate_result = plugins::discover_portable_wasm_plugin_catalog(duplicate, project, backend);
  CHECK(has_issue(duplicate_result, plugins::PortableWasmPluginCatalogIssueCode::DuplicatePackage));
  CHECK(backend.calls == 0);
}

TEST_CASE("Portable WASM plugin catalog: packages load in canonical path order") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  const auto plugin_directory = directory.path() / "plugins";
  REQUIRE(std::filesystem::create_directory(plugin_directory));
  for (const std::string_view name : {"z-last", "a-first"}) {
    const auto package = plugin_directory / (std::string{name} + ".plugin");
    REQUIRE(std::filesystem::create_directory(package));
    write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  }
  const modules::ProductDescriptor product{
      .name = "wasm-catalog",
      .plugins = {"plugins/z-last.plugin", "plugins/a-first.plugin"},
  };
  FakeWasmBackend backend;
  backend.provider_ids = {"mobagen.a-first", "mobagen.z-last"};

  const auto result = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend);

  REQUIRE(result.ok());
  REQUIRE(result.catalog->plugin_count() == 2);
  REQUIRE(result.catalog->plugin(0) != nullptr);
  REQUIRE(result.catalog->plugin(1) != nullptr);
  CHECK(result.catalog->plugin(0)->path().parent_path().filename() == "a-first.plugin");
  CHECK(result.catalog->plugin(0)->provider().id == "mobagen.a-first");
  CHECK(result.catalog->plugin(1)->path().parent_path().filename() == "z-last.plugin");
  CHECK(result.catalog->plugin(1)->provider().id == "mobagen.z-last");
}

TEST_CASE("Portable WASM plugin catalog: duplicate provider metadata rejects the staged registry") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  const auto plugin_directory = directory.path() / "plugins";
  REQUIRE(std::filesystem::create_directory(plugin_directory));
  for (const std::string_view name : {"first", "second"}) {
    const auto package = plugin_directory / (std::string{name} + ".plugin");
    REQUIRE(std::filesystem::create_directory(package));
    write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  }
  const modules::ProductDescriptor product{
      .name = "wasm-catalog",
      .plugins = {"plugins/first.plugin", "plugins/second.plugin"},
  };
  FakeWasmBackend backend;

  const auto result = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend);

  CHECK_FALSE(result.ok());
  CHECK(has_issue(result, plugins::PortableWasmPluginCatalogIssueCode::RegistryFailed));
  CHECK(backend.calls == 2);
}
