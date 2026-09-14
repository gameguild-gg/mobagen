#include <doctest/doctest.h>

#include <algorithm>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "modules/resolver.hpp"

namespace {

  mobagen::modules::ProviderDescriptor resolver_provider(std::string id, std::vector<std::string> capabilities) {
    using namespace mobagen::modules;

    return {
        .id = std::move(id),
        .version = {1, 0, 0},
        .provides = std::move(capabilities),
        .targets = {TargetPlatform::Windows},
        .linkages = {LinkageMode::Static, LinkageMode::Dynamic},
    };
  }

  mobagen::modules::ProductDescriptor resolver_product(std::string provider) {
    using namespace mobagen::modules;

    return {
        .schema = project_schema_version,
        .name = "resolver-test",
        .modules = {{.alias = "render", .provider = std::move(provider)}},
        .profiles = {{.name = "release", .linkage = LinkageMode::Static, .editor = false}},
    };
  }

  mobagen::modules::ResolverOptions resolver_options() {
    using namespace mobagen::modules;

    return {
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
  }

  mobagen::modules::CapabilityRegistry make_resolver_registry(bool reverse_discovery) {
    using namespace mobagen::modules;

    const auto null_renderer = resolver_provider("mobagen.render.null", {"render.backend.v1"});
    const auto webgpu_renderer = resolver_provider("mobagen.render.webgpu", {"render.backend.v1"});
    CapabilityRegistryBuilder builder;
    if (reverse_discovery) {
      builder.add(webgpu_renderer);
      builder.add(null_renderer);
    } else {
      builder.add(null_renderer);
      builder.add(webgpu_renderer);
    }
    auto result = builder.build();
    REQUIRE(result.ok());
    return std::move(*result.registry);
  }

  mobagen::modules::CapabilityRegistry build_resolver_registry(std::vector<mobagen::modules::ProviderDescriptor> providers) {
    mobagen::modules::CapabilityRegistryBuilder builder;
    for (auto& provider : providers) builder.add(std::move(provider));
    auto result = builder.build();
    REQUIRE(result.ok());
    return std::move(*result.registry);
  }

  bool has_resolution_issue(const mobagen::modules::ResolutionResult& result, mobagen::modules::ResolutionIssueCode code,
                            std::string_view capability = {}) {
    return std::ranges::any_of(result.issues,
                               [=](const auto& issue) { return issue.code == code && (capability.empty() || issue.capability == capability); });
  }

  std::vector<std::string> provider_ids(const mobagen::modules::CapabilityRegistry& registry,
                                        std::span<const mobagen::modules::ProviderIndex> providers) {
    std::vector<std::string> ids;
    for (const auto provider : providers) {
      const auto* descriptor = registry.provider(provider);
      REQUIRE(descriptor != nullptr);
      ids.push_back(descriptor->id);
    }
    return ids;
  }

}  // namespace

TEST_CASE("Module resolver: profile default is deterministic across discovery order") {
  using namespace mobagen::modules;

  const auto forward_registry = make_resolver_registry(false);
  const auto reverse_registry = make_resolver_registry(true);
  const auto product = resolver_product("default");
  const auto options = resolver_options();

  const auto forward = resolve_modules(product, forward_registry, options);
  const auto reverse = resolve_modules(product, reverse_registry, options);

  REQUIRE(forward.ok());
  REQUIRE(reverse.ok());
  const auto capability = forward_registry.find_capability("render.backend.v1");
  REQUIRE(capability.has_value());
  const auto* forward_selection = forward.resolution->selection_for(*capability);
  const auto* reverse_selection = reverse.resolution->selection_for(*capability);
  REQUIRE(forward_selection != nullptr);
  REQUIRE(reverse_selection != nullptr);
  REQUIRE(forward_registry.provider(forward_selection->provider) != nullptr);
  REQUIRE(reverse_registry.provider(reverse_selection->provider) != nullptr);
  CHECK(forward_registry.provider(forward_selection->provider)->id == "mobagen.render.webgpu");
  CHECK(reverse_registry.provider(reverse_selection->provider)->id == "mobagen.render.webgpu");
  CHECK(forward_selection->linkage == LinkageMode::Static);
  CHECK(forward_selection->reason == "default for profile 'release'");
  REQUIRE(forward.resolution->lifecycle_order().size() == 1);
  CHECK(forward.resolution->lifecycle_order().front() == forward_selection->provider);
}

TEST_CASE("Module resolver: an explicit provider overrides the profile default") {
  using namespace mobagen::modules;

  const auto registry = make_resolver_registry(false);
  const auto result = resolve_modules(resolver_product("mobagen.render.null"), registry, resolver_options());

  REQUIRE(result.ok());
  const auto capability = registry.find_capability("render.backend.v1");
  REQUIRE(capability.has_value());
  const auto* selection = result.resolution->selection_for(*capability);
  REQUIRE(selection != nullptr);
  REQUIRE(registry.provider(selection->provider) != nullptr);
  CHECK(registry.provider(selection->provider)->id == "mobagen.render.null");
  CHECK(selection->reason == "explicit provider for module 'render'");
}

TEST_CASE("Module resolver: required capabilities produce a stable dependency order") {
  using namespace mobagen::modules;

  auto renderer = resolver_provider("mobagen.render.webgpu", {"render.backend.v1"});
  renderer.required = {"window.surface.v1"};
  auto window = resolver_provider("mobagen.window.sdl3", {"window.surface.v1"});
  window.required = {"platform.events.v1"};
  auto platform = resolver_provider("mobagen.platform.sdl3", {"platform.events.v1"});
  const auto registry = build_resolver_registry({window, renderer, platform});

  const auto result = resolve_modules(resolver_product("default"), registry, resolver_options());

  REQUIRE(result.ok());
  CHECK(result.resolution->selections().size() == 3);
  CHECK(provider_ids(registry, result.resolution->lifecycle_order())
        == std::vector<std::string>{"mobagen.platform.sdl3", "mobagen.window.sdl3", "mobagen.render.webgpu"});
  REQUIRE(result.resolution->dependencies().size() == 2);
  const auto& first_edge = result.resolution->dependencies()[0];
  const auto& second_edge = result.resolution->dependencies()[1];
  REQUIRE(registry.provider(first_edge.dependency) != nullptr);
  REQUIRE(registry.provider(first_edge.dependent) != nullptr);
  REQUIRE(registry.provider(second_edge.dependency) != nullptr);
  REQUIRE(registry.provider(second_edge.dependent) != nullptr);
  CHECK(registry.provider(first_edge.dependency)->id == "mobagen.platform.sdl3");
  CHECK(registry.provider(first_edge.dependent)->id == "mobagen.window.sdl3");
  CHECK(registry.capability_name(first_edge.capability) == "platform.events.v1");
  CHECK(registry.provider(second_edge.dependency)->id == "mobagen.window.sdl3");
  CHECK(registry.provider(second_edge.dependent)->id == "mobagen.render.webgpu");
  CHECK(registry.capability_name(second_edge.capability) == "window.surface.v1");
}

TEST_CASE("Module resolver: missing and ambiguous required capabilities are errors") {
  using namespace mobagen::modules;

  auto renderer = resolver_provider("mobagen.render.webgpu", {"render.backend.v1"});
  renderer.required = {"window.surface.v1"};

  SUBCASE("missing") {
    const auto registry = build_resolver_registry({renderer});
    const auto result = resolve_modules(resolver_product("default"), registry, resolver_options());

    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.resolution.has_value());
    CHECK(has_resolution_issue(result, ResolutionIssueCode::MissingCapability, "window.surface.v1"));
  }

  SUBCASE("ambiguous") {
    const auto sdl = resolver_provider("mobagen.window.sdl3", {"window.surface.v1"});
    const auto headless = resolver_provider("mobagen.window.headless", {"window.surface.v1"});
    const auto registry = build_resolver_registry({renderer, sdl, headless});
    const auto result = resolve_modules(resolver_product("default"), registry, resolver_options());

    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.resolution.has_value());
    CHECK(has_resolution_issue(result, ResolutionIssueCode::AmbiguousProvider, "window.surface.v1"));
  }
}

TEST_CASE("Module resolver: a profile default resolves a required-capability tie") {
  using namespace mobagen::modules;

  auto renderer = resolver_provider("mobagen.render.webgpu", {"render.backend.v1"});
  renderer.required = {"window.surface.v1"};
  const auto sdl = resolver_provider("mobagen.window.sdl3", {"window.surface.v1"});
  const auto headless = resolver_provider("mobagen.window.headless", {"window.surface.v1"});
  const auto registry = build_resolver_registry({headless, renderer, sdl});
  auto options = resolver_options();
  options.defaults.push_back({
      .target = TargetPlatform::Windows,
      .profile = "release",
      .capability = "window.surface.v1",
      .provider = "mobagen.window.sdl3",
  });

  const auto result = resolve_modules(resolver_product("default"), registry, options);

  REQUIRE(result.ok());
  const auto window_capability = registry.find_capability("window.surface.v1");
  REQUIRE(window_capability.has_value());
  const auto* window_selection = result.resolution->selection_for(*window_capability);
  REQUIRE(window_selection != nullptr);
  REQUIRE(registry.provider(window_selection->provider) != nullptr);
  CHECK(registry.provider(window_selection->provider)->id == "mobagen.window.sdl3");
}

TEST_CASE("Module resolver: conflicts and dependency cycles reject the staged graph") {
  using namespace mobagen::modules;

  SUBCASE("conflict") {
    auto renderer = resolver_provider("mobagen.render.webgpu", {"render.backend.v1"});
    renderer.required = {"window.surface.v1"};
    renderer.conflicts = {"mobagen.window.sdl3"};
    const auto window = resolver_provider("mobagen.window.sdl3", {"window.surface.v1"});
    const auto registry = build_resolver_registry({renderer, window});
    const auto result = resolve_modules(resolver_product("default"), registry, resolver_options());

    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.resolution.has_value());
    CHECK(has_resolution_issue(result, ResolutionIssueCode::ProviderConflict));
  }

  SUBCASE("cycle") {
    auto renderer = resolver_provider("mobagen.render.webgpu", {"render.backend.v1"});
    renderer.required = {"window.surface.v1"};
    auto window = resolver_provider("mobagen.window.sdl3", {"window.surface.v1"});
    window.required = {"render.backend.v1"};
    const auto registry = build_resolver_registry({renderer, window});
    const auto result = resolve_modules(resolver_product("default"), registry, resolver_options());

    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.resolution.has_value());
    CHECK(has_resolution_issue(result, ResolutionIssueCode::DependencyCycle));
  }
}

TEST_CASE("Module resolver: selected providers require explicit profile permissions") {
  using namespace mobagen::modules;

  auto renderer = resolver_provider("mobagen.render.webgpu", {"render.backend.v1"});
  renderer.permissions = {"filesystem-read", "gpu"};
  const auto registry = build_resolver_registry({renderer});

  SUBCASE("all requested permissions are granted") {
    auto product = resolver_product("default");
    product.profiles.front().permissions = {"gpu", "filesystem-read"};

    const auto result = resolve_modules(product, registry, resolver_options());

    REQUIRE(result.ok());
  }

  SUBCASE("a missing grant rejects resolution") {
    auto product = resolver_product("default");
    product.profiles.front().permissions = {"gpu"};

    const auto result = resolve_modules(product, registry, resolver_options());

    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.resolution.has_value());
    CHECK(has_resolution_issue(result, ResolutionIssueCode::PermissionDenied));
    REQUIRE(result.issues.size() == 1);
    CHECK(result.issues.front().provider_id == "mobagen.render.webgpu");
    CHECK(result.issues.front().message.contains("filesystem-read"));
    CHECK(result.issues.front().message.contains("release"));
  }
}

TEST_CASE("Module resolver: invalid selection contracts return structured errors") {
  using namespace mobagen::modules;

  const auto renderer = resolver_provider("mobagen.render.webgpu", {"render.backend.v1"});
  const auto window = resolver_provider("mobagen.window.sdl3", {"window.surface.v1"});

  SUBCASE("unknown explicit provider") {
    const auto registry = build_resolver_registry({renderer});
    const auto result = resolve_modules(resolver_product("customer.render.missing"), registry, resolver_options());
    CHECK(has_resolution_issue(result, ResolutionIssueCode::UnknownProvider));
  }

  SUBCASE("explicit provider lacks the requested capability") {
    const auto registry = build_resolver_registry({renderer, window});
    const auto result = resolve_modules(resolver_product("mobagen.window.sdl3"), registry, resolver_options());
    CHECK(has_resolution_issue(result, ResolutionIssueCode::ProviderDoesNotProvide, "render.backend.v1"));
  }

  SUBCASE("unsupported target") {
    auto linux_renderer = renderer;
    linux_renderer.targets = {TargetPlatform::Linux};
    const auto registry = build_resolver_registry({linux_renderer});
    const auto result = resolve_modules(resolver_product("default"), registry, resolver_options());
    CHECK(has_resolution_issue(result, ResolutionIssueCode::UnsupportedTarget));
  }

  SUBCASE("unsupported linkage") {
    auto dynamic_renderer = renderer;
    dynamic_renderer.linkages = {LinkageMode::Dynamic};
    const auto registry = build_resolver_registry({dynamic_renderer});
    const auto result = resolve_modules(resolver_product("default"), registry, resolver_options());
    CHECK(has_resolution_issue(result, ResolutionIssueCode::UnsupportedLinkage));
  }

  SUBCASE("missing default") {
    const auto registry = build_resolver_registry({renderer});
    auto options = resolver_options();
    options.defaults.clear();
    const auto result = resolve_modules(resolver_product("default"), registry, options);
    CHECK(has_resolution_issue(result, ResolutionIssueCode::MissingDefault, "render.backend.v1"));
  }

  SUBCASE("default names an unknown provider") {
    const auto registry = build_resolver_registry({renderer});
    auto options = resolver_options();
    options.defaults.front().provider = "customer.render.missing";
    const auto result = resolve_modules(resolver_product("default"), registry, options);
    CHECK(has_resolution_issue(result, ResolutionIssueCode::UnknownProvider, "render.backend.v1"));
  }
}

TEST_CASE("Module resolver: duplicate alias and default tables are rejected deterministically") {
  using namespace mobagen::modules;

  const auto registry = build_resolver_registry({resolver_provider("mobagen.render.webgpu", {"render.backend.v1"})});

  SUBCASE("duplicate alias") {
    auto options = resolver_options();
    options.aliases.push_back(options.aliases.front());
    const auto result = resolve_modules(resolver_product("default"), registry, options);
    CHECK_FALSE(result.ok());
    CHECK(has_resolution_issue(result, ResolutionIssueCode::DuplicateAlias, "render.backend.v1"));
  }

  SUBCASE("duplicate default") {
    auto options = resolver_options();
    options.defaults.push_back(options.defaults.front());
    const auto result = resolve_modules(resolver_product("default"), registry, options);
    CHECK_FALSE(result.ok());
    CHECK(has_resolution_issue(result, ResolutionIssueCode::DuplicateDefault, "render.backend.v1"));
  }
}
