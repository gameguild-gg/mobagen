#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

#include "modules/catalog_index.hpp"
#include "modules/catalog_parser.hpp"
#include "modules/resolver.hpp"

namespace {

  bool has_error(const mobagen::modules::CatalogParseResult& result, mobagen::modules::CatalogErrorCode code, std::string_view field) {
    return std::ranges::any_of(result.errors, [=](const auto& error) { return error.code == code && error.field == field; });
  }

}  // namespace

TEST_CASE("Module catalog: provider metadata and artifacts parse without loading executable code") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: 1
providers:
  mobagen.render.webgpu:
    version: 1.4.2
    provides:
      - render.backend.v1
    requires:
      - window.surface.v1
    optional:
      - telemetry.sink.v1
    conflicts:
      - customer.render.legacy
    reload: restart
    configuration-schema: mobagen.render.config.v1
    permissions:
      - gpu
    artifacts:
      - target: windows
        linkage: dynamic
        url: https://plugins.mobagen.dev/mobagen.render.webgpu/1.4.2/windows.plugin
        size: 1048576
        hash: sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
      - target: web
        linkage: wasm
        url: https://plugins.mobagen.dev/mobagen.render.webgpu/1.4.2/web.plugin
        size: 524288
        hash: sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
)yaml";

  const auto result = parse_module_catalog(source, "official/catalog.yaml");

  REQUIRE(result.ok());
  REQUIRE(result.catalog.has_value());
  REQUIRE(result.catalog->providers.size() == 1);
  const auto& published = result.catalog->providers.front();
  CHECK(published.provider.id == "mobagen.render.webgpu");
  CHECK(published.provider.version == SemanticVersion{1, 4, 2});
  CHECK(published.provider.provides == std::vector<std::string>{"render.backend.v1"});
  CHECK(published.provider.required == std::vector<std::string>{"window.surface.v1"});
  CHECK(published.provider.optional == std::vector<std::string>{"telemetry.sink.v1"});
  CHECK(published.provider.conflicts == std::vector<std::string>{"customer.render.legacy"});
  CHECK(published.provider.reload == ReloadPolicy::Restart);
  CHECK(published.provider.configuration_schema == "mobagen.render.config.v1");
  CHECK(published.provider.permissions == std::vector<std::string>{"gpu"});
  CHECK(published.provider.targets == std::vector<TargetPlatform>{TargetPlatform::Windows, TargetPlatform::Web});
  CHECK(published.provider.linkages == std::vector<LinkageMode>{LinkageMode::Dynamic, LinkageMode::Wasm});
  REQUIRE(published.artifacts.size() == 2);
  CHECK(published.artifacts[0].target == TargetPlatform::Windows);
  CHECK(published.artifacts[0].linkage == LinkageMode::Dynamic);
  CHECK(published.artifacts[0].size == 1048576);
}

TEST_CASE("Module catalog: artifacts require HTTPS, canonical hashes, bounded sizes, and unique targets") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: 1
providers:
  mobagen.render.invalid:
    version: 1.0.0
    provides: [render.backend.v1]
    artifacts:
      - target: windows
        linkage: dynamic
        url: http://plugins.mobagen.dev/invalid.plugin
        size: 0
        hash: sha256:not-a-hash
      - target: windows
        linkage: dynamic
        url: https://plugins.mobagen.dev/duplicate.plugin
        size: 1
        hash: sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
      - target: linux
        linkage: dynamic
        url: https://plugins.mobagen.dev/not-a-plugin.bin?release=1
        size: 1
        hash: sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
)yaml";

  const auto result = parse_module_catalog(source, "catalog.yaml");

  CHECK_FALSE(result.ok());
  CHECK(has_error(result, CatalogErrorCode::InvalidValue, "providers.mobagen.render.invalid.artifacts[0].url"));
  CHECK(has_error(result, CatalogErrorCode::InvalidValue, "providers.mobagen.render.invalid.artifacts[0].size"));
  CHECK(has_error(result, CatalogErrorCode::InvalidValue, "providers.mobagen.render.invalid.artifacts[0].hash"));
  CHECK(has_error(result, CatalogErrorCode::DuplicateEntry, "providers.mobagen.render.invalid.artifacts[1]"));
  CHECK(has_error(result, CatalogErrorCode::InvalidValue, "providers.mobagen.render.invalid.artifacts[2].url"));
}

TEST_CASE("Module catalog: unknown fields, duplicate providers, and custom tags fail transactionally") {
  using namespace mobagen::modules;

  constexpr std::string_view source = R"yaml(schema: 1
providers:
  mobagen.runtime.tick:
    version: 1.0.0
    provides: [runtime.tick.v1]
    artifacts:
      - target: windows
        linkage: dynamic
        url: !include https://plugins.mobagen.dev/runtime.plugin
        size: 1
        hash: sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    script: forbidden
  mobagen.runtime.tick:
    version: 1.0.1
    provides: [runtime.tick.v1]
    artifacts: []
)yaml";

  const auto result = parse_module_catalog(source, "catalog.yaml");

  CHECK_FALSE(result.ok());
  CHECK_FALSE(result.catalog.has_value());
  CHECK(has_error(result, CatalogErrorCode::UnsupportedTag, "providers.mobagen.runtime.tick.artifacts[0].url"));
  CHECK(has_error(result, CatalogErrorCode::UnknownField, "providers.mobagen.runtime.tick.script"));
  CHECK(has_error(result, CatalogErrorCode::DuplicateKey, "providers.mobagen.runtime.tick"));
}

TEST_CASE("Module catalog: capability resolution selects metadata before the artifact is loaded") {
  using namespace mobagen::modules;

  ProviderDescriptor provider{
      .id = "mobagen.render.webgpu",
      .version = {1, 4, 2},
      .provides = {"render.backend.v1"},
      .targets = {TargetPlatform::Windows, TargetPlatform::Web},
      .linkages = {LinkageMode::Dynamic, LinkageMode::Wasm},
  };
  ModuleCatalogDescriptor catalog{
      .providers = {{
          .provider = provider,
          .artifacts = {
              {.target = TargetPlatform::Windows,
               .linkage = LinkageMode::Dynamic,
               .url = "https://plugins.mobagen.dev/render/windows.plugin",
               .size = 128,
               .hash = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
              {.target = TargetPlatform::Web,
               .linkage = LinkageMode::Wasm,
               .url = "https://plugins.mobagen.dev/render/web.plugin",
               .size = 64,
               .hash = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
          },
      }},
  };

  auto indexed = build_module_catalog_index(std::span(&catalog, 1), TargetPlatform::Windows, LinkageMode::Dynamic);
  REQUIRE(indexed.ok());
  const auto provider_index = indexed.index->registry().find_provider("mobagen.render.webgpu");
  REQUIRE(provider_index.has_value());
  const auto* artifact = indexed.index->artifact_for(*provider_index);
  REQUIRE(artifact != nullptr);
  CHECK(artifact->url == "https://plugins.mobagen.dev/render/windows.plugin");

  ProductDescriptor product{
      .name = "catalog-resolution",
      .modules = {{.alias = "render", .provider = "default"}},
      .profiles = {{.name = "editor", .linkage = LinkageMode::Dynamic, .editor = true}},
  };
  ResolverOptions options{
      .target = TargetPlatform::Windows,
      .profile = "editor",
      .aliases = {{.alias = "render", .capability = "render.backend.v1"}},
      .defaults = {{TargetPlatform::Windows, "editor", "render.backend.v1", "mobagen.render.webgpu"}},
  };
  const auto resolved = resolve_modules(product, indexed.index->registry(), options);
  REQUIRE(resolved.ok());
  REQUIRE(resolved.resolution->lifecycle_order().size() == 1);
  CHECK(resolved.resolution->lifecycle_order().front() == *provider_index);

  const auto unsupported = build_module_catalog_index(std::span(&catalog, 1), TargetPlatform::Web, LinkageMode::Dynamic);
  REQUIRE(unsupported.ok());
  CHECK(unsupported.index->registry().provider_count() == 0);
}

TEST_CASE("Module catalog: duplicate providers across sources fail deterministically") {
  using namespace mobagen::modules;

  const PublishedProviderDescriptor published{
      .provider = {
          .id = "mobagen.runtime.tick",
          .version = {1, 0, 0},
          .provides = {"runtime.tick.v1"},
          .targets = {TargetPlatform::Windows},
          .linkages = {LinkageMode::Dynamic},
      },
      .artifacts = {{
          .target = TargetPlatform::Windows,
          .linkage = LinkageMode::Dynamic,
          .url = "https://plugins.mobagen.dev/runtime.plugin",
          .size = 1,
          .hash = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      }},
  };
  const std::array catalogs{ModuleCatalogDescriptor{.providers = {published}}, ModuleCatalogDescriptor{.providers = {published}}};

  const auto result = build_module_catalog_index(catalogs, TargetPlatform::Windows, LinkageMode::Dynamic);

  CHECK_FALSE(result.ok());
  CHECK(std::ranges::any_of(result.issues, [](const CatalogIndexIssue& issue) {
    return issue.code == CatalogIndexIssueCode::RegistryFailed
           && std::ranges::any_of(issue.registry_issues,
                                  [](const RegistryIssue& registry) { return registry.code == RegistryIssueCode::DuplicateProvider; });
  }));
}
