#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "modules/capability_registry.hpp"

namespace {

  mobagen::modules::ProviderDescriptor make_provider(std::string id, std::vector<std::string> capabilities) {
    using namespace mobagen::modules;

    return {
        .id = std::move(id),
        .version = {1, 0, 0},
        .provides = std::move(capabilities),
        .targets = {TargetPlatform::Windows, TargetPlatform::Linux},
        .linkages = {LinkageMode::Static, LinkageMode::Dynamic},
    };
  }

  bool has_registry_issue(const std::vector<mobagen::modules::RegistryIssue>& issues, mobagen::modules::RegistryIssueCode code,
                          std::string_view provider_id) {
    return std::ranges::any_of(issues, [=](const auto& issue) { return issue.code == code && issue.provider_id == provider_id; });
  }

  bool has_descriptor_cause(const std::vector<mobagen::modules::RegistryIssue>& issues, mobagen::modules::DescriptorIssueCode code,
                            std::string_view field) {
    return std::ranges::any_of(issues, [=](const auto& issue) {
      return std::ranges::any_of(issue.descriptor_issues, [=](const auto& cause) { return cause.code == code && cause.field == field; });
    });
  }

}  // namespace

TEST_CASE("Module registry: provider and capability indices ignore discovery order") {
  using namespace mobagen::modules;

  const auto null_renderer = make_provider("mobagen.render.null", {"render.backend.v1"});
  const auto webgpu_renderer = make_provider("mobagen.render.webgpu", {"render.debug.v1", "render.backend.v1"});

  CapabilityRegistryBuilder forward_builder;
  forward_builder.add(webgpu_renderer);
  forward_builder.add(null_renderer);
  const auto forward = forward_builder.build();

  CapabilityRegistryBuilder reverse_builder;
  reverse_builder.add(null_renderer);
  reverse_builder.add(webgpu_renderer);
  const auto reverse = reverse_builder.build();

  REQUIRE(forward.ok());
  REQUIRE(reverse.ok());
  const auto& forward_registry = *forward.registry;
  const auto& reverse_registry = *reverse.registry;

  CHECK(forward_registry.provider_count() == 2);
  CHECK(forward_registry.capability_count() == 2);
  CHECK(forward_registry.find_provider("mobagen.render.null") == ProviderIndex{0});
  CHECK(forward_registry.find_provider("mobagen.render.webgpu") == ProviderIndex{1});
  CHECK(forward_registry.find_capability("render.backend.v1") == CapabilityIndex{0});
  CHECK(forward_registry.find_capability("render.debug.v1") == CapabilityIndex{1});

  REQUIRE(forward_registry.provider(ProviderIndex{0}) != nullptr);
  REQUIRE(forward_registry.provider(ProviderIndex{1}) != nullptr);
  CHECK(forward_registry.provider(ProviderIndex{0})->id == "mobagen.render.null");
  CHECK(forward_registry.provider(ProviderIndex{1})->id == "mobagen.render.webgpu");
  CHECK(forward_registry.capability_name(CapabilityIndex{0}) == "render.backend.v1");
  CHECK(forward_registry.capability_name(CapabilityIndex{1}) == "render.debug.v1");

  constexpr std::array backend_providers{ProviderIndex{0}, ProviderIndex{1}};
  constexpr std::array debug_providers{ProviderIndex{1}};
  CHECK(std::ranges::equal(forward_registry.providers_for(CapabilityIndex{0}), backend_providers));
  CHECK(std::ranges::equal(forward_registry.providers_for(CapabilityIndex{1}), debug_providers));

  CHECK(reverse_registry.find_provider("mobagen.render.null") == forward_registry.find_provider("mobagen.render.null"));
  CHECK(reverse_registry.find_provider("mobagen.render.webgpu") == forward_registry.find_provider("mobagen.render.webgpu"));
  CHECK(reverse_registry.find_capability("render.backend.v1") == forward_registry.find_capability("render.backend.v1"));
  CHECK(reverse_registry.find_capability("render.debug.v1") == forward_registry.find_capability("render.debug.v1"));
  CHECK(std::ranges::equal(reverse_registry.providers_for(CapabilityIndex{0}), backend_providers));
  CHECK(std::ranges::equal(reverse_registry.providers_for(CapabilityIndex{1}), debug_providers));
}

TEST_CASE("Module registry: invalid and duplicate providers fail transactionally") {
  using namespace mobagen::modules;

  auto invalid = make_provider("WebGPU", {"render.backend.v1", "render.backend.v1"});
  const auto duplicate = make_provider("mobagen.render.null", {"render.backend.v1"});

  CapabilityRegistryBuilder builder;
  builder.add(duplicate);
  builder.add(invalid);
  builder.add(duplicate);
  const auto result = builder.build();

  CHECK_FALSE(result.ok());
  CHECK_FALSE(result.registry.has_value());
  CHECK(has_registry_issue(result.issues, RegistryIssueCode::InvalidDescriptor, "WebGPU"));
  CHECK(has_registry_issue(result.issues, RegistryIssueCode::DuplicateProvider, "mobagen.render.null"));
  CHECK(has_descriptor_cause(result.issues, DescriptorIssueCode::InvalidIdentifier, "id"));
  CHECK(has_descriptor_cause(result.issues, DescriptorIssueCode::DuplicateEntry, "provides[1]"));
}

TEST_CASE("Module registry: missing names and out-of-range indices are safe") {
  using namespace mobagen::modules;

  CapabilityRegistryBuilder builder;
  builder.add(make_provider("mobagen.render.null", {"render.backend.v1"}));
  const auto result = builder.build();

  REQUIRE(result.ok());
  const auto& registry = *result.registry;
  CHECK_FALSE(registry.find_provider("mobagen.render.missing").has_value());
  CHECK_FALSE(registry.find_capability("render.missing.v1").has_value());
  CHECK(registry.provider(ProviderIndex{99}) == nullptr);
  CHECK(registry.capability_name(CapabilityIndex{99}).empty());
  CHECK(registry.providers_for(CapabilityIndex{99}).empty());
}
