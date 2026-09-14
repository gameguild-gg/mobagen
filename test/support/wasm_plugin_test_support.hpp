#pragma once

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "assets/asset_id.hpp"
#include "plugins/wasm_plugin_loader.hpp"

namespace mobagen::test {

  inline constexpr std::array valid_wasm_header{
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

  inline void write_binary(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.is_open());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output.good());
  }

  inline void write_text(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.is_open());
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    REQUIRE(output.good());
  }

  inline std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.is_open());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  }

  inline std::string valid_wasm_hash() {
    const auto hash = assets::sha256(valid_wasm_header);
    REQUIRE(hash.has_value());
    return assets::to_string(*hash);
  }

  inline modules::TargetPlatform portable_target() {
#if defined(__EMSCRIPTEN__)
    return modules::TargetPlatform::Web;
#elif defined(_WIN32)
    return modules::TargetPlatform::Windows;
#elif defined(__ANDROID__)
    return modules::TargetPlatform::Android;
#elif defined(__APPLE__)
    return modules::TargetPlatform::MacOS;
#else
    return modules::TargetPlatform::Linux;
#endif
  }

  inline std::string portable_target_name() {
    switch (portable_target()) {
      case modules::TargetPlatform::Windows:
        return "windows";
      case modules::TargetPlatform::Linux:
        return "linux";
      case modules::TargetPlatform::MacOS:
        return "macos";
      case modules::TargetPlatform::Web:
        return "web";
      case modules::TargetPlatform::Android:
        return "android";
      case modules::TargetPlatform::IOS:
        return "ios";
    }
    return {};
  }

  inline void write_u32(std::vector<std::byte>& memory, std::size_t offset, std::uint32_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
      memory[offset + byte] = std::byte{static_cast<std::uint8_t>(value >> (byte * 8U))};
    }
  }

  inline void write_string(std::vector<std::byte>& memory, std::uint32_t offset, std::string_view value) {
    std::ranges::transform(value, memory.begin() + offset, [](char byte) { return std::byte{static_cast<std::uint8_t>(byte)}; });
  }

  class DescriptorInstance final : public plugins::PortableWasmInstance {
  public:
    explicit DescriptorInstance(std::shared_ptr<std::vector<plugins::WasmPluginExport>> invocations,
                                std::string provider_id = "mobagen.wasm-package", std::string capability_id = "runtime.package.v1",
                                std::uint32_t start_status = MOBAGEN_WASM_STATUS_OK)
        : invocations_(std::move(invocations)),
          provider_id_(std::move(provider_id)),
          capability_id_(std::move(capability_id)),
          start_status_(start_status) {}

    plugins::WasmInvocationResult invoke(plugins::WasmPluginExport function, std::span<const std::uint32_t> arguments) override {
      invocations_->push_back(function);
      if (function == plugins::WasmPluginExport::Allocate) {
        return plugins::WasmInvocationResult::success(8);
      }
      if (function == plugins::WasmPluginExport::Query) {
        encode_descriptor(arguments[0]);
        return plugins::WasmInvocationResult::success(MOBAGEN_WASM_STATUS_OK);
      }
      if (function == plugins::WasmPluginExport::Deallocate) {
        return plugins::WasmInvocationResult::success(MOBAGEN_WASM_STATUS_OK);
      }
      if (function == plugins::WasmPluginExport::Start) {
        return plugins::WasmInvocationResult::success(start_status_);
      }
      if (function == plugins::WasmPluginExport::Configure || function == plugins::WasmPluginExport::Quiesce
          || function == plugins::WasmPluginExport::Stop) {
        return plugins::WasmInvocationResult::success(MOBAGEN_WASM_STATUS_OK);
      }
      return plugins::WasmInvocationResult::failure("unexpected export");
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

    std::shared_ptr<std::vector<plugins::WasmPluginExport>> invocations_;
    std::string provider_id_;
    std::string capability_id_;
    std::uint32_t start_status_;
    std::vector<std::byte> linear_memory = std::vector<std::byte>(256);
  };

  class FakeWasmBackend final : public plugins::PortableWasmBackend {
  public:
    plugins::PortableWasmInstantiationResult instantiate(std::span<const std::byte> binary) override {
      ++calls;
      observed.assign(binary.begin(), binary.end());
      if (throws) throw std::runtime_error{"backend trapped"};
      if (fails) return plugins::PortableWasmInstantiationResult::failure("backend rejected module");
      const auto index = calls - 1;
      const auto provider_id = provider_ids.empty() ? std::string{"mobagen.wasm-package"} : provider_ids.at(index);
      const auto capability_id = capability_ids.empty() ? std::string{"runtime.package.v1"} : capability_ids.at(index);
      const auto start_status = start_statuses.empty() ? MOBAGEN_WASM_STATUS_OK : start_statuses.at(index);
      auto instance = std::make_unique<DescriptorInstance>(invocations, provider_id, capability_id, start_status);
      instance->malformed = malformed_descriptor;
      return plugins::PortableWasmInstantiationResult::success(std::move(instance));
    }

    std::vector<std::byte> observed;
    std::shared_ptr<std::vector<plugins::WasmPluginExport>> invocations = std::make_shared<std::vector<plugins::WasmPluginExport>>();
    std::size_t calls{};
    std::vector<std::string> provider_ids;
    std::vector<std::string> capability_ids;
    std::vector<std::uint32_t> start_statuses;
    bool fails{};
    bool throws{};
    bool malformed_descriptor{};
  };

  inline std::size_t invocation_count(const FakeWasmBackend& backend, plugins::WasmPluginExport function) {
    return static_cast<std::size_t>(std::ranges::count(*backend.invocations, function));
  }

}  // namespace mobagen::test
