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

#include "portable/project_runtime.hpp"
#include "plugins/wasm_plugin_activation_set.hpp"

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

  void write_text(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.is_open());
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    REQUIRE(output.good());
  }

  mobagen::modules::TargetPlatform portable_target() {
#if defined(__EMSCRIPTEN__)
    return mobagen::modules::TargetPlatform::Web;
#elif defined(_WIN32)
    return mobagen::modules::TargetPlatform::Windows;
#elif defined(__ANDROID__)
    return mobagen::modules::TargetPlatform::Android;
#elif defined(__APPLE__)
    return mobagen::modules::TargetPlatform::MacOS;
#else
    return mobagen::modules::TargetPlatform::Linux;
#endif
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
                                std::string provider_id = "mobagen.wasm-package", std::string capability_id = "runtime.package.v1",
                                std::uint32_t start_status = MOBAGEN_WASM_STATUS_OK)
        : invocations_(std::move(invocations)),
          provider_id_(std::move(provider_id)),
          capability_id_(std::move(capability_id)),
          start_status_(start_status) {}

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
      if (function == mobagen::plugins::WasmPluginExport::Start) {
        return mobagen::plugins::WasmInvocationResult::success(start_status_);
      }
      if (function == mobagen::plugins::WasmPluginExport::Configure || function == mobagen::plugins::WasmPluginExport::Quiesce
          || function == mobagen::plugins::WasmPluginExport::Stop) {
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
      write_string(linear_memory, id_offset, provider_id_);
      write_string(linear_memory, capability_offset, capability_id_);
      write_u32(linear_memory, provides_offset, capability_offset);
      write_u32(linear_memory, provides_offset + 4, static_cast<std::uint32_t>(capability_id_.size()));
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
    std::string capability_id_;
    std::uint32_t start_status_;
    std::vector<std::byte> linear_memory = std::vector<std::byte>(256);
  };

  class FakeWasmBackend final : public mobagen::plugins::PortableWasmBackend {
  public:
    mobagen::plugins::PortableWasmInstantiationResult instantiate(std::span<const std::byte> binary) override {
      ++calls;
      observed.assign(binary.begin(), binary.end());
      if (throws) throw std::runtime_error{"backend trapped"};
      if (fails) return mobagen::plugins::PortableWasmInstantiationResult::failure("backend rejected module");
      const auto index = calls - 1;
      const auto provider_id = provider_ids.empty() ? std::string{"mobagen.wasm-package"} : provider_ids.at(index);
      const auto capability_id = capability_ids.empty() ? std::string{"runtime.package.v1"} : capability_ids.at(index);
      const auto start_status = start_statuses.empty() ? MOBAGEN_WASM_STATUS_OK : start_statuses.at(index);
      auto instance = std::make_unique<DescriptorInstance>(invocations, provider_id, capability_id, start_status);
      instance->malformed = malformed_descriptor;
      return mobagen::plugins::PortableWasmInstantiationResult::success(std::move(instance));
    }

    std::vector<std::byte> observed;
    std::shared_ptr<std::vector<mobagen::plugins::WasmPluginExport>> invocations
        = std::make_shared<std::vector<mobagen::plugins::WasmPluginExport>>();
    std::size_t calls{};
    std::vector<std::string> provider_ids;
    std::vector<std::string> capability_ids;
    std::vector<std::uint32_t> start_statuses;
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

  std::size_t invocation_count(const FakeWasmBackend& backend, mobagen::plugins::WasmPluginExport function) {
    return static_cast<std::size_t>(std::ranges::count(*backend.invocations, function));
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

TEST_CASE("Resolved portable WASM plugin activation: manifest selection activates without requery") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  const auto package = directory.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  const modules::ProductDescriptor product{
      .name = "resolved-wasm",
      .modules = {{.alias = "runtime", .provider = "mobagen.wasm-package"}},
      .plugins = {"plugins/reference.plugin"},
      .profiles = {{.name = "release", .linkage = modules::LinkageMode::Wasm, .editor = false}},
  };
  FakeWasmBackend backend;
  auto catalog = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend);
  REQUIRE(catalog.ok());
  const auto target = catalog.catalog->plugin(0)->provider().targets.front();
  const auto resolution
      = modules::resolve_modules(product, catalog.catalog->registry(),
                                 {.target = target, .profile = "release", .aliases = {{.alias = "runtime", .capability = "runtime.package.v1"}}});
  REQUIRE(resolution.ok());

  auto activated = plugins::activate_resolved_portable_wasm_plugins(*catalog.catalog, *resolution.resolution);

  REQUIRE(activated.ok());
  CHECK(activated.activation->size() == 1);
  REQUIRE(activated.activation->plugin(0) != nullptr);
  CHECK(activated.activation->plugin(0)->provider().id == "mobagen.wasm-package");
  CHECK(activated.activation->plugin(1) == nullptr);
  CHECK(catalog.catalog->plugin_count() == 0);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Query) == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Configure) == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Start) == 1);
  CHECK(activated.activation->stop().ok());
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Quiesce) == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Stop) == 1);

  const auto repeated = plugins::activate_resolved_portable_wasm_plugins(*catalog.catalog, *resolution.resolution);
  CHECK_FALSE(repeated.ok());
  REQUIRE_FALSE(repeated.issues.empty());
  CHECK(repeated.issues.front().code == plugins::ResolvedPortableWasmPluginIssueCode::PluginUnavailable);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Configure) == 1);
}

TEST_CASE("Resolved portable WASM plugin activation: selected failure rolls back the entire set") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  for (const std::string_view name : {"first", "second"}) {
    const auto package = directory.path() / "plugins" / (std::string{name} + ".plugin");
    REQUIRE(std::filesystem::create_directory(package));
    write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  }
  const modules::ProductDescriptor product{
      .name = "resolved-wasm",
      .modules = {
          {.alias = "first", .provider = "mobagen.first"},
          {.alias = "second", .provider = "mobagen.second"},
      },
      .plugins = {"plugins/first.plugin", "plugins/second.plugin"},
      .profiles = {{.name = "release", .linkage = modules::LinkageMode::Wasm, .editor = false}},
  };
  FakeWasmBackend backend;
  backend.provider_ids = {"mobagen.first", "mobagen.second"};
  backend.capability_ids = {"runtime.first.v1", "runtime.second.v1"};
  backend.start_statuses = {MOBAGEN_WASM_STATUS_OK, MOBAGEN_WASM_STATUS_FAILED};
  auto catalog = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend);
  REQUIRE(catalog.ok());
  const auto target = catalog.catalog->plugin(0)->provider().targets.front();
  const auto resolution = modules::resolve_modules(product, catalog.catalog->registry(),
                                                   {.target = target,
                                                    .profile = "release",
                                                    .aliases = {
                                                        {.alias = "first", .capability = "runtime.first.v1"},
                                                        {.alias = "second", .capability = "runtime.second.v1"},
                                                    }});
  REQUIRE(resolution.ok());

  const auto activated = plugins::activate_resolved_portable_wasm_plugins(*catalog.catalog, *resolution.resolution);

  CHECK_FALSE(activated.ok());
  REQUIRE_FALSE(activated.issues.empty());
  CHECK(activated.issues.front().code == plugins::ResolvedPortableWasmPluginIssueCode::ActivationFailed);
  CHECK(activated.issues.front().provider_id == "mobagen.second");
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Configure) == 2);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Start) == 2);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Quiesce) == 2);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Stop) == 2);
}

TEST_CASE("Resolved portable WASM plugin activation: builtins remain outside the plugin lifecycle") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  const auto package = directory.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  modules::ProductDescriptor product{
      .name = "resolved-wasm",
      .modules = {{.alias = "runtime", .provider = "mobagen.builtin"}},
      .plugins = {"plugins/reference.plugin"},
      .profiles = {{.name = "release", .linkage = modules::LinkageMode::Wasm, .editor = false}},
  };
  FakeWasmBackend backend;
  const modules::ProviderDescriptor builtin{
      .id = "mobagen.builtin",
      .version = {1, 0, 0},
      .provides = {"runtime.package.v1"},
      .targets = {
          modules::TargetPlatform::Windows,
          modules::TargetPlatform::Linux,
          modules::TargetPlatform::MacOS,
          modules::TargetPlatform::Web,
          modules::TargetPlatform::Android,
          modules::TargetPlatform::IOS,
      },
      .linkages = {modules::LinkageMode::Wasm},
  };
  auto catalog = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend, std::span(&builtin, 1));
  REQUIRE(catalog.ok());
  const auto runtime_target = catalog.catalog->plugin(0)->provider().targets.front();
  const auto resolution = modules::resolve_modules(
      product, catalog.catalog->registry(),
      {.target = runtime_target, .profile = "release", .aliases = {{.alias = "runtime", .capability = "runtime.package.v1"}}});
  REQUIRE(resolution.ok());

  const auto activated = plugins::activate_resolved_portable_wasm_plugins(*catalog.catalog, *resolution.resolution);

  REQUIRE(activated.ok());
  CHECK(activated.activation->size() == 0);
  CHECK(catalog.catalog->plugin_count() == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Configure) == 0);
}

TEST_CASE("Resolved portable WASM plugin activation: foreign resolutions are rejected before consumption") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  const auto package = directory.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  const modules::ProductDescriptor product{
      .name = "resolved-wasm",
      .modules = {{.alias = "runtime", .provider = "mobagen.wasm-package"}},
      .plugins = {"plugins/reference.plugin"},
      .profiles = {{.name = "release", .linkage = modules::LinkageMode::Wasm, .editor = false}},
  };
  FakeWasmBackend backend;
  auto catalog = plugins::discover_portable_wasm_plugin_catalog(product, directory.path(), backend);
  REQUIRE(catalog.ok());
  modules::CapabilityRegistryBuilder other_builder;
  other_builder.add(catalog.catalog->plugin(0)->provider());
  const auto other_registry = other_builder.build();
  REQUIRE(other_registry.ok());
  const auto target = catalog.catalog->plugin(0)->provider().targets.front();
  const auto resolution
      = modules::resolve_modules(product, *other_registry.registry,
                                 {.target = target, .profile = "release", .aliases = {{.alias = "runtime", .capability = "runtime.package.v1"}}});
  REQUIRE(resolution.ok());

  const auto activated = plugins::activate_resolved_portable_wasm_plugins(*catalog.catalog, *resolution.resolution);

  CHECK_FALSE(activated.ok());
  REQUIRE_FALSE(activated.issues.empty());
  CHECK(activated.issues.front().code == plugins::ResolvedPortableWasmPluginIssueCode::InvalidResolution);
  CHECK(catalog.catalog->plugin_count() == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Configure) == 0);
}

TEST_CASE("Portable project: mobagen yaml resolves and activates a dot-plugin end to end") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  const auto package = directory.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  write_text(directory.path() / "mobagen.yaml", R"yaml(schema: 1
name: portable-project-test
modules:
  runtime:
    use: mobagen.wasm-package
plugins:
  - ./plugins/reference.plugin
profiles:
  release:
    linkage: wasm
    editor: false
)yaml");
  FakeWasmBackend backend;
  const modules::ResolverOptions options{
      .target = portable_target(),
      .profile = "release",
      .aliases = {{.alias = "runtime", .capability = "runtime.package.v1"}},
  };

  auto loaded = compositions::load_portable_project(directory.path() / "mobagen.yaml", options, backend);

  REQUIRE(loaded.ok());
  CHECK(loaded.runtime->product().name == "portable-project-test");
  CHECK(loaded.runtime->registry().provider_count() == 1);
  CHECK(loaded.runtime->resolution().lifecycle_order().size() == 1);
  REQUIRE(loaded.runtime->plugin(0) != nullptr);
  CHECK(loaded.runtime->plugin(0)->provider().id == "mobagen.wasm-package");
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Query) == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Start) == 1);
  CHECK(loaded.runtime->stop().ok());
}

TEST_CASE("Portable project: input and activation failures never publish a partial runtime") {
  using namespace mobagen;
  TemporaryWasmDirectory directory;
  FakeWasmBackend backend;
  const modules::ResolverOptions options{
      .target = portable_target(),
      .profile = "release",
      .aliases = {{.alias = "runtime", .capability = "runtime.package.v1"}},
  };

  const auto missing = compositions::load_portable_project(directory.path() / "mobagen.yaml", options, backend);
  CHECK_FALSE(missing.ok());
  REQUIRE_FALSE(missing.issues.empty());
  CHECK(missing.issues.front().code == compositions::PortableProjectIssueCode::ReadManifest);
  CHECK(backend.calls == 0);

  write_text(directory.path() / "mobagen.yaml", "schema: [1\n");
  const auto malformed = compositions::load_portable_project(directory.path() / "mobagen.yaml", options, backend);
  CHECK_FALSE(malformed.ok());
  REQUIRE_FALSE(malformed.issues.empty());
  CHECK(malformed.issues.front().code == compositions::PortableProjectIssueCode::ParseManifest);
  CHECK(backend.calls == 0);

  write_text(directory.path() / "mobagen.yaml", std::string(modules::max_product_manifest_bytes + 1, 'x'));
  const auto oversized = compositions::load_portable_project(directory.path() / "mobagen.yaml", options, backend);
  CHECK_FALSE(oversized.ok());
  REQUIRE_FALSE(oversized.issues.empty());
  CHECK(oversized.issues.front().code == compositions::PortableProjectIssueCode::ReadManifest);
  CHECK(backend.calls == 0);

  REQUIRE(std::filesystem::create_directory(directory.path() / "plugins"));
  const auto package = directory.path() / "plugins/reference.plugin";
  REQUIRE(std::filesystem::create_directory(package));
  write_binary(package / plugins::portable_wasm_plugin_binary_filename(), valid_wasm_header);
  write_text(directory.path() / "mobagen.yaml", R"yaml(schema: 1
name: rejected-portable-project
modules:
  runtime:
    use: mobagen.wasm-package
plugins:
  - ./plugins/reference.plugin
profiles:
  release:
    linkage: wasm
    editor: false
)yaml");
  backend.start_statuses = {MOBAGEN_WASM_STATUS_FAILED};

  const auto rejected = compositions::load_portable_project(directory.path() / "mobagen.yaml", options, backend);

  CHECK_FALSE(rejected.ok());
  REQUIRE_FALSE(rejected.issues.empty());
  CHECK(rejected.issues.front().code == compositions::PortableProjectIssueCode::Activation);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Quiesce) == 1);
  CHECK(invocation_count(backend, plugins::WasmPluginExport::Stop) == 1);
}
