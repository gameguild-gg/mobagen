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
#include <vector>

#include "plugins/wasm_plugin_loader.hpp"

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
    mobagen::plugins::WasmInvocationResult invoke(mobagen::plugins::WasmPluginExport function, std::span<const std::uint32_t> arguments) override {
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
      constexpr std::string_view id = "mobagen.wasm-package";
      constexpr std::string_view capability = "runtime.package.v1";
      write_string(linear_memory, id_offset, id);
      write_string(linear_memory, capability_offset, capability);
      write_u32(linear_memory, provides_offset, capability_offset);
      write_u32(linear_memory, provides_offset + 4, static_cast<std::uint32_t>(capability.size()));
      write_u32(linear_memory, descriptor_offset, malformed ? 0 : MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
      write_u32(linear_memory, descriptor_offset + 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
      write_u32(linear_memory, descriptor_offset + 8, id_offset);
      write_u32(linear_memory, descriptor_offset + 12, static_cast<std::uint32_t>(id.size()));
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

    std::vector<std::byte> linear_memory = std::vector<std::byte>(256);
  };

  class FakeWasmBackend final : public mobagen::plugins::PortableWasmBackend {
  public:
    mobagen::plugins::PortableWasmInstantiationResult instantiate(std::span<const std::byte> binary) override {
      ++calls;
      observed.assign(binary.begin(), binary.end());
      if (throws) throw std::runtime_error{"backend trapped"};
      if (fails) return mobagen::plugins::PortableWasmInstantiationResult::failure("backend rejected module");
      auto instance = std::make_unique<DescriptorInstance>();
      instance->malformed = malformed_descriptor;
      return mobagen::plugins::PortableWasmInstantiationResult::success(std::move(instance));
    }

    std::vector<std::byte> observed;
    std::size_t calls{};
    bool fails{};
    bool throws{};
    bool malformed_descriptor{};
  };

  bool has_issue(const mobagen::plugins::PortableWasmPluginLoadResult& result, mobagen::plugins::PortableWasmPluginLoadIssueCode code) {
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
