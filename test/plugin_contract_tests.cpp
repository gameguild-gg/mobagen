#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "plugins/plugin_contract.hpp"

namespace {

  MobagenStringView view(std::string_view value) { return {value.data(), value.size()}; }

  MobagenStatus MOBAGEN_PLUGIN_CALL configure(void*, const MobagenHostApiV1*, MobagenByteView) { return MOBAGEN_STATUS_OK; }
  MobagenStatus MOBAGEN_PLUGIN_CALL start(void*) { return MOBAGEN_STATUS_OK; }
  MobagenStatus MOBAGEN_PLUGIN_CALL quiesce(void*) { return MOBAGEN_STATUS_OK; }
  void MOBAGEN_PLUGIN_CALL stop(void*) {}
  void MOBAGEN_PLUGIN_CALL destroy(void*) {}

  MobagenPluginDescriptorV1 valid_descriptor() {
    static const std::array provides{view("render.backend.v1")};
    static const std::array required{view("render.target.v1")};
    static int plugin_state = 0;
    return {
        .struct_size = MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE,
        .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
        .id = view("customer.renderer"),
        .version_major = 1,
        .version_minor = 2,
        .version_patch = 3,
        .reload_policy = MOBAGEN_RELOAD_RESTART,
        .plugin_state = &plugin_state,
        .provides = provides.data(),
        .provides_count = static_cast<std::uint32_t>(provides.size()),
        .required = required.data(),
        .required_count = static_cast<std::uint32_t>(required.size()),
        .optional = nullptr,
        .optional_count = 0,
        .conflicts = nullptr,
        .conflicts_count = 0,
        .lifecycle =
            {
                .struct_size = MOBAGEN_PLUGIN_LIFECYCLE_V1_SIZE,
                .configure = configure,
                .start = start,
                .quiesce = quiesce,
                .stop = stop,
                .destroy = destroy,
            },
    };
  }

  bool has_issue(const mobagen::plugins::PluginContractResult& result, mobagen::plugins::PluginContractIssueCode code) {
    return std::ranges::any_of(result.issues, [code](const auto& issue) { return issue.code == code; });
  }

}  // namespace

TEST_CASE("Plugin contract: valid ABI descriptor becomes an owned provider contract") {
  using namespace mobagen::plugins;
  const auto descriptor = valid_descriptor();
  const auto result = validate_native_plugin(descriptor);

  REQUIRE(result.contract.has_value());
  CHECK(result.issues.empty());
  CHECK(result.contract->provider.id == "customer.renderer");
  CHECK(result.contract->provider.version == (mobagen::modules::SemanticVersion{1, 2, 3}));
  CHECK(result.contract->provider.provides == std::vector<std::string>{"render.backend.v1"});
  CHECK(result.contract->provider.required == std::vector<std::string>{"render.target.v1"});
  CHECK(result.contract->provider.linkages == std::vector{mobagen::modules::LinkageMode::Dynamic});
  CHECK_FALSE(result.contract->provider.targets.empty());
  CHECK(result.contract->provider.reload == mobagen::modules::ReloadPolicy::Restart);
  CHECK(result.contract->plugin_state == descriptor.plugin_state);
  CHECK(result.contract->lifecycle.start == descriptor.lifecycle.start);
}

TEST_CASE("Plugin contract: incompatible and truncated ABI structures are rejected") {
  using namespace mobagen::plugins;
  auto descriptor = valid_descriptor();
  descriptor.abi_version = MOBAGEN_PLUGIN_ABI_VERSION + 1;
  descriptor.struct_size = sizeof(std::uint32_t);
  descriptor.lifecycle.struct_size = 0;
  const auto result = validate_native_plugin(descriptor);

  CHECK_FALSE(result.contract.has_value());
  CHECK(has_issue(result, PluginContractIssueCode::unsupported_abi));
  CHECK(has_issue(result, PluginContractIssueCode::truncated_descriptor));
  CHECK(has_issue(result, PluginContractIssueCode::truncated_lifecycle));
}

TEST_CASE("Plugin contract: pointer and count validation happens before array access") {
  using namespace mobagen::plugins;
  auto descriptor = valid_descriptor();
  descriptor.provides = nullptr;
  descriptor.provides_count = max_plugin_capabilities + 1;
  descriptor.required = nullptr;
  descriptor.required_count = 1;
  const auto result = validate_native_plugin(descriptor);

  CHECK_FALSE(result.contract.has_value());
  CHECK(has_issue(result, PluginContractIssueCode::limit_exceeded));
  CHECK(has_issue(result, PluginContractIssueCode::missing_array));
}

TEST_CASE("Plugin contract: invalid strings lifecycle and provider semantics are rejected") {
  using namespace mobagen::plugins;
  auto descriptor = valid_descriptor();
  constexpr char invalid_id[] = {'b', 'a', 'd', '\0', 'i', 'd'};
  descriptor.id = {invalid_id, sizeof(invalid_id)};
  descriptor.lifecycle.start = nullptr;
  const auto result = validate_native_plugin(descriptor);

  CHECK_FALSE(result.contract.has_value());
  CHECK(has_issue(result, PluginContractIssueCode::invalid_string));
  CHECK(has_issue(result, PluginContractIssueCode::invalid_lifecycle));
}
