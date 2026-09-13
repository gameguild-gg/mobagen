#include <doctest/doctest.h>

#include <algorithm>
#include <string_view>
#include <vector>

#include "modules/descriptor.hpp"

namespace {

  using mobagen::modules::DescriptorIssue;
  using mobagen::modules::DescriptorIssueCode;

  bool has_issue(const std::vector<DescriptorIssue>& issues, DescriptorIssueCode code, std::string_view field) {
    return std::ranges::any_of(issues, [=](const DescriptorIssue& issue) { return issue.code == code && issue.field == field; });
  }

}  // namespace

TEST_CASE("Module descriptor: canonical product intent is valid") {
  using namespace mobagen::modules;

  ProductDescriptor descriptor{
      .schema = project_schema_version,
      .name = "dicom-viewer",
      .modules = {{.alias = "render", .provider = "default"}, {.alias = "volume-importer", .provider = "mobagen.import.dicom"}},
      .plugins = {"./plugins/custom-transfer.plugin"},
      .profiles = {{.name = "editor", .linkage = LinkageMode::Dynamic}, {.name = "web", .linkage = LinkageMode::Static, .editor = false}},
  };

  CHECK(validate(descriptor).empty());
}

TEST_CASE("Module descriptor: identifiers use a deterministic ASCII grammar") {
  using namespace mobagen::modules;

  CHECK(is_slug("volume-importer"));
  CHECK(is_provider_id("mobagen.render.webgpu"));
  CHECK(is_capability_id("render.backend.v1"));
  CHECK_FALSE(is_slug("Volume-Importer"));
  CHECK_FALSE(is_provider_id("mobagen-.render"));
  CHECK_FALSE(is_provider_id("mobagen..render"));
  CHECK_FALSE(is_capability_id("render.backend"));
  CHECK_FALSE(is_capability_id("render.backend.vnext"));
}

TEST_CASE("Module descriptor: invalid product intent reports every structural problem") {
  using namespace mobagen::modules;

  ProductDescriptor descriptor{
      .schema = 99,
      .name = "Invalid Product Name",
      .modules = {{.alias = "render", .provider = "not a provider"}, {.alias = "render", .provider = "default"}},
      .plugins = {"./plugins/a.plugin", "./plugins/a.plugin", "./plugins/not-a-plugin.zip"},
      .profiles = {{.name = "release", .linkage = LinkageMode::Static}, {.name = "release", .linkage = LinkageMode::Dynamic}},
  };

  const auto issues = validate(descriptor);

  CHECK(has_issue(issues, DescriptorIssueCode::UnsupportedSchema, "schema"));
  CHECK(has_issue(issues, DescriptorIssueCode::InvalidIdentifier, "name"));
  CHECK(has_issue(issues, DescriptorIssueCode::InvalidIdentifier, "modules.render.use"));
  CHECK(has_issue(issues, DescriptorIssueCode::DuplicateEntry, "modules.render"));
  CHECK(has_issue(issues, DescriptorIssueCode::DuplicateEntry, "plugins[1]"));
  CHECK(has_issue(issues, DescriptorIssueCode::InvalidPluginPath, "plugins[2]"));
  CHECK(has_issue(issues, DescriptorIssueCode::DuplicateEntry, "profiles.release"));
}

TEST_CASE("Module descriptor: provider contract validates capabilities and deployment modes") {
  using namespace mobagen::modules;

  ProviderDescriptor provider{
      .id = "mobagen.render.webgpu",
      .version = {1, 4, 2},
      .provides = {"render.backend.v1"},
      .required = {"window.surface.v1"},
      .optional = {"debug.markers.v1"},
      .conflicts = {"mobagen.render.vulkan"},
      .targets = {TargetPlatform::Windows, TargetPlatform::Linux, TargetPlatform::MacOS, TargetPlatform::Web},
      .linkages = {LinkageMode::Static, LinkageMode::Dynamic, LinkageMode::Wasm},
      .reload = ReloadPolicy::Restart,
      .configuration_schema = "render.webgpu.v1",
      .permissions = {"gpu"},
  };

  CHECK(validate(provider).empty());

  provider.id = "WebGPU";
  provider.provides = {"render.backend.v1", "render.backend.v1", "invalid"};
  provider.required = {"render.backend.v1"};
  provider.targets.clear();
  provider.linkages.clear();

  const auto issues = validate(provider);
  CHECK(has_issue(issues, DescriptorIssueCode::InvalidIdentifier, "id"));
  CHECK(has_issue(issues, DescriptorIssueCode::DuplicateEntry, "provides[1]"));
  CHECK(has_issue(issues, DescriptorIssueCode::InvalidCapability, "provides[2]"));
  CHECK(has_issue(issues, DescriptorIssueCode::SelfDependency, "required[0]"));
  CHECK(has_issue(issues, DescriptorIssueCode::MissingEntry, "targets"));
  CHECK(has_issue(issues, DescriptorIssueCode::MissingEntry, "linkages"));
}
