#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "modules/lockfile.hpp"

namespace {

  mobagen::modules::ProviderDescriptor lockfile_provider(std::string id, mobagen::modules::SemanticVersion version,
                                                         std::vector<std::string> capabilities) {
    using namespace mobagen::modules;

    return {
        .id = std::move(id),
        .version = version,
        .provides = std::move(capabilities),
        .targets = {TargetPlatform::Windows},
        .linkages = {LinkageMode::Static},
    };
  }

  struct ResolvedFixture {
    mobagen::modules::CapabilityRegistry registry;
    mobagen::modules::ModuleResolution resolution;
  };

  ResolvedFixture make_resolved_fixture() {
    using namespace mobagen::modules;

    auto renderer = lockfile_provider("mobagen.render.webgpu", {1, 4, 2}, {"render.backend.v1"});
    renderer.required = {"window.surface.v1"};
    auto window = lockfile_provider("mobagen.window.sdl3", {3, 1, 0}, {"window.surface.v1"});

    CapabilityRegistryBuilder registry_builder;
    registry_builder.add(std::move(renderer));
    registry_builder.add(std::move(window));
    auto registry_result = registry_builder.build();
    REQUIRE(registry_result.ok());
    auto registry = std::move(*registry_result.registry);

    ProductDescriptor product{
        .schema = project_schema_version,
        .name = "lockfile-test",
        .modules = {{.alias = "render", .provider = "default"}},
        .profiles = {{.name = "release", .linkage = LinkageMode::Static, .editor = false}},
    };
    ResolverOptions options{
        .target = TargetPlatform::Windows,
        .profile = "release",
        .aliases = {{.alias = "render", .capability = "render.backend.v1"}},
        .defaults = {{
            .target = TargetPlatform::Windows,
            .profile = "release",
            .capability = "render.backend.v1",
            .provider = "mobagen.render.webgpu",
        }},
    };
    auto resolution_result = resolve_modules(product, registry, options);
    REQUIRE(resolution_result.ok());
    return {std::move(registry), std::move(*resolution_result.resolution)};
  }

  bool has_lockfile_issue(const mobagen::modules::LockfileSerializeResult& result, mobagen::modules::LockfileIssueCode code, std::string_view field) {
    return std::ranges::any_of(result.issues, [=](const auto& issue) { return issue.code == code && issue.field == field; });
  }

}  // namespace

TEST_CASE("Module lockfile: serialization is canonical and independent of plugin order") {
  using namespace mobagen::modules;

  const auto fixture = make_resolved_fixture();
  constexpr std::string_view first_hash = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  constexpr std::string_view second_hash = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  LockfileMetadata metadata{
      .sdk = {1, 2, 3},
      .target = TargetPlatform::Windows,
      .profile = "release",
      .plugins = {
          {.provider = "customer.transfer", .version = {2, 0, 1}, .abi_version = 1, .hash = std::string(second_hash)},
          {.provider = "customer.color", .version = {1, 5, 0}, .abi_version = 1, .hash = std::string(first_hash)},
      },
  };

  const auto serialized = serialize_lockfile(fixture.registry, fixture.resolution, metadata);
  std::ranges::reverse(metadata.plugins);
  const auto reversed = serialize_lockfile(fixture.registry, fixture.resolution, metadata);

  constexpr std::string_view expected
      = "schema: 1\n"
        "sdk: 1.2.3\n"
        "target: windows\n"
        "profile: release\n"
        "resolved:\n"
        "  render.backend.v1:\n"
        "    provider: mobagen.render.webgpu\n"
        "    version: 1.4.2\n"
        "    linkage: static\n"
        "  window.surface.v1:\n"
        "    provider: mobagen.window.sdl3\n"
        "    version: 3.1.0\n"
        "    linkage: static\n"
        "dependencies:\n"
        "  - capability: window.surface.v1\n"
        "    provider: mobagen.window.sdl3\n"
        "    required-by: mobagen.render.webgpu\n"
        "plugins:\n"
        "  customer.color:\n"
        "    version: 1.5.0\n"
        "    abi: 1\n"
        "    hash: sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
        "  customer.transfer:\n"
        "    version: 2.0.1\n"
        "    abi: 1\n"
        "    hash: sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n";

  REQUIRE(serialized.ok());
  REQUIRE(reversed.ok());
  CHECK(*serialized.contents == expected);
  CHECK(*reversed.contents == expected);
}

TEST_CASE("Module lockfile: invalid metadata returns issues without partial YAML") {
  using namespace mobagen::modules;

  const auto fixture = make_resolved_fixture();
  LockfileMetadata metadata{
      .schema = 9,
      .sdk = {1, 0, 0},
      .target = TargetPlatform::Windows,
      .profile = "Invalid Profile",
      .plugins = {
          {.provider = "customer.color", .version = {1, 0, 0}, .abi_version = 1, .hash = "sha256:not-a-digest"},
          {.provider = "customer.color", .version = {1, 1, 0}, .abi_version = 1, .hash = "sha256:short"},
      },
  };

  const auto result = serialize_lockfile(fixture.registry, fixture.resolution, metadata);

  CHECK_FALSE(result.ok());
  CHECK_FALSE(result.contents.has_value());
  CHECK(has_lockfile_issue(result, LockfileIssueCode::UnsupportedSchema, "schema"));
  CHECK(has_lockfile_issue(result, LockfileIssueCode::InvalidValue, "profile"));
  CHECK(has_lockfile_issue(result, LockfileIssueCode::InvalidHash, "plugins.customer.color.hash"));
  CHECK(has_lockfile_issue(result, LockfileIssueCode::DuplicateEntry, "plugins.customer.color"));
}
