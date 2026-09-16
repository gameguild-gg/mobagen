#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "plugins/wasm_plugin_contract.hpp"

namespace {

  struct EncodedSpan {
    std::uint32_t offset{};
    std::uint32_t size{};
  };

  struct DescriptorFixture {
    std::vector<std::byte> memory = std::vector<std::byte>(MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
    EncodedSpan id;
    EncodedSpan first_provided;
  };

  void write_u32(std::vector<std::byte>& memory, std::size_t offset, std::uint32_t value) {
    REQUIRE(offset <= memory.size());
    REQUIRE(sizeof(value) <= memory.size() - offset);
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
      memory[offset + byte] = std::byte{static_cast<std::uint8_t>(value >> (byte * 8U))};
    }
  }

  void align_u32(std::vector<std::byte>& memory) {
    while (memory.size() % alignof(std::uint32_t) != 0) memory.push_back(std::byte{});
  }

  EncodedSpan append_string(std::vector<std::byte>& memory, std::string_view value) {
    const auto offset = static_cast<std::uint32_t>(memory.size());
    for (const char byte : value) memory.push_back(std::byte{static_cast<std::uint8_t>(byte)});
    return {offset, static_cast<std::uint32_t>(value.size())};
  }

  EncodedSpan append_array(std::vector<std::byte>& memory, std::initializer_list<std::string_view> values, EncodedSpan* first = nullptr) {
    std::vector<EncodedSpan> strings;
    strings.reserve(values.size());
    for (const auto value : values) strings.push_back(append_string(memory, value));
    align_u32(memory);
    const auto offset = static_cast<std::uint32_t>(memory.size());
    memory.resize(memory.size() + strings.size() * MOBAGEN_WASM_SPAN32_SIZE);
    for (std::size_t index = 0; index < strings.size(); ++index) {
      write_u32(memory, offset + index * MOBAGEN_WASM_SPAN32_SIZE, strings[index].offset);
      write_u32(memory, offset + index * MOBAGEN_WASM_SPAN32_SIZE + 4, strings[index].size);
    }
    if (first != nullptr && !strings.empty()) *first = strings.front();
    return {offset, static_cast<std::uint32_t>(strings.size())};
  }

  DescriptorFixture valid_descriptor() {
    DescriptorFixture fixture;
    fixture.id = append_string(fixture.memory, "customer.renderer");
    const auto provides = append_array(fixture.memory, {"render.backend.v1"}, &fixture.first_provided);
    const auto required = append_array(fixture.memory, {"render.target.v1"});
    const auto configuration_schema = append_string(fixture.memory, "customer.renderer.config.v1");
    const auto permissions = append_array(fixture.memory, {"gpu"});

    write_u32(fixture.memory, 0, MOBAGEN_WASM_PLUGIN_DESCRIPTOR_V1_SIZE);
    write_u32(fixture.memory, 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION);
    write_u32(fixture.memory, 8, fixture.id.offset);
    write_u32(fixture.memory, 12, fixture.id.size);
    write_u32(fixture.memory, 16, 1);
    write_u32(fixture.memory, 20, 2);
    write_u32(fixture.memory, 24, 3);
    write_u32(fixture.memory, 28, MOBAGEN_WASM_RELOAD_SAFE_POINT);
    write_u32(fixture.memory, 32, provides.offset);
    write_u32(fixture.memory, 36, provides.size);
    write_u32(fixture.memory, 40, required.offset);
    write_u32(fixture.memory, 44, required.size);
    write_u32(fixture.memory, 48, 0);
    write_u32(fixture.memory, 52, 0);
    write_u32(fixture.memory, 56, 0);
    write_u32(fixture.memory, 60, 0);
    write_u32(fixture.memory, 64, configuration_schema.offset);
    write_u32(fixture.memory, 68, configuration_schema.size);
    write_u32(fixture.memory, 72, permissions.offset);
    write_u32(fixture.memory, 76, permissions.size);
    return fixture;
  }

  bool has_issue(const mobagen::plugins::WasmPluginContractResult& result, mobagen::plugins::WasmPluginContractIssueCode code) {
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
  }

}  // namespace

TEST_CASE("WASM plugin contract: a valid descriptor becomes owned host metadata") {
  auto fixture = valid_descriptor();
  const auto result = mobagen::plugins::decode_wasm_plugin_descriptor(fixture.memory, 0);

  REQUIRE(result.provider.has_value());
  CHECK(result.issues.empty());
  CHECK(result.provider->id == "customer.renderer");
  CHECK(result.provider->version == (mobagen::modules::SemanticVersion{1, 2, 3}));
  CHECK(result.provider->provides == std::vector<std::string>{"render.backend.v1"});
  CHECK(result.provider->required == std::vector<std::string>{"render.target.v1"});
  CHECK(result.provider->linkages == std::vector{mobagen::modules::LinkageMode::Wasm});
  CHECK(result.provider->reload == mobagen::modules::ReloadPolicy::SafePoint);
  CHECK(result.provider->configuration_schema == "customer.renderer.config.v1");
  CHECK(result.provider->permissions == std::vector<std::string>{"gpu"});

  fixture.memory[fixture.id.offset] = std::byte{static_cast<std::uint8_t>('x')};
  CHECK(result.provider->id == "customer.renderer");
}

TEST_CASE("WASM plugin contract: descriptor and nested spans are bounds checked") {
  auto fixture = valid_descriptor();
  const auto missing_header
      = mobagen::plugins::decode_wasm_plugin_descriptor(fixture.memory, static_cast<std::uint32_t>(fixture.memory.size() - sizeof(std::uint32_t)));
  CHECK_FALSE(missing_header.provider.has_value());
  CHECK(has_issue(missing_header, mobagen::plugins::WasmPluginContractIssueCode::OutOfBounds));

  write_u32(fixture.memory, 32, 0xfffffff8U);
  const auto missing_array = mobagen::plugins::decode_wasm_plugin_descriptor(fixture.memory, 0);
  CHECK_FALSE(missing_array.provider.has_value());
  CHECK(has_issue(missing_array, mobagen::plugins::WasmPluginContractIssueCode::OutOfBounds));

  fixture = valid_descriptor();
  write_u32(fixture.memory, 12, MOBAGEN_WASM_MAX_STRING_BYTES + 1);
  const auto oversized_string = mobagen::plugins::decode_wasm_plugin_descriptor(fixture.memory, 0);
  CHECK_FALSE(oversized_string.provider.has_value());
  CHECK(has_issue(oversized_string, mobagen::plugins::WasmPluginContractIssueCode::LimitExceeded));
}

TEST_CASE("WASM plugin contract: array limits alignment and strings fail closed") {
  auto fixture = valid_descriptor();
  write_u32(fixture.memory, 36, MOBAGEN_WASM_MAX_DESCRIPTOR_ENTRIES + 1);
  const auto oversized_array = mobagen::plugins::decode_wasm_plugin_descriptor(fixture.memory, 0);
  CHECK_FALSE(oversized_array.provider.has_value());
  CHECK(has_issue(oversized_array, mobagen::plugins::WasmPluginContractIssueCode::LimitExceeded));

  fixture = valid_descriptor();
  write_u32(fixture.memory, 32, 81);
  const auto misaligned_array = mobagen::plugins::decode_wasm_plugin_descriptor(fixture.memory, 0);
  CHECK_FALSE(misaligned_array.provider.has_value());
  CHECK(has_issue(misaligned_array, mobagen::plugins::WasmPluginContractIssueCode::Misaligned));

  fixture = valid_descriptor();
  fixture.memory[fixture.first_provided.offset + 2] = std::byte{};
  const auto embedded_null = mobagen::plugins::decode_wasm_plugin_descriptor(fixture.memory, 0);
  CHECK_FALSE(embedded_null.provider.has_value());
  CHECK(has_issue(embedded_null, mobagen::plugins::WasmPluginContractIssueCode::InvalidString));
}

TEST_CASE("WASM plugin contract: ABI reload and provider semantics are validated") {
  auto fixture = valid_descriptor();
  write_u32(fixture.memory, 4, MOBAGEN_WASM_PLUGIN_ABI_VERSION + 1);
  const auto unsupported = mobagen::plugins::decode_wasm_plugin_descriptor(fixture.memory, 0);
  CHECK_FALSE(unsupported.provider.has_value());
  CHECK(has_issue(unsupported, mobagen::plugins::WasmPluginContractIssueCode::UnsupportedAbi));

  fixture = valid_descriptor();
  write_u32(fixture.memory, 28, 99);
  const auto invalid_reload = mobagen::plugins::decode_wasm_plugin_descriptor(fixture.memory, 0);
  CHECK_FALSE(invalid_reload.provider.has_value());
  CHECK(has_issue(invalid_reload, mobagen::plugins::WasmPluginContractIssueCode::InvalidReloadPolicy));

  fixture = valid_descriptor();
  fixture.memory[fixture.id.offset] = std::byte{static_cast<std::uint8_t>('C')};
  const auto invalid_provider = mobagen::plugins::decode_wasm_plugin_descriptor(fixture.memory, 0);
  CHECK_FALSE(invalid_provider.provider.has_value());
  CHECK(has_issue(invalid_provider, mobagen::plugins::WasmPluginContractIssueCode::InvalidProviderDescriptor));
}
