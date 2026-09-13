#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "modules/lifecycle.hpp"

namespace {

  mobagen::modules::ProviderDescriptor lifecycle_provider(std::string id, std::string capability, std::vector<std::string> required = {}) {
    using namespace mobagen::modules;

    return {
        .id = std::move(id),
        .version = {1, 0, 0},
        .provides = {std::move(capability)},
        .required = std::move(required),
        .targets = {TargetPlatform::Windows},
        .linkages = {LinkageMode::Static},
    };
  }

  struct LifecycleFixture {
    mobagen::modules::CapabilityRegistry registry;
    mobagen::modules::ModuleResolution resolution;
  };

  LifecycleFixture make_lifecycle_fixture() {
    using namespace mobagen::modules;

    CapabilityRegistryBuilder registry_builder;
    registry_builder.add(lifecycle_provider("mobagen.application", "application.runtime.v1", {"simulation.runtime.v1"}));
    registry_builder.add(lifecycle_provider("mobagen.clock", "clock.runtime.v1"));
    registry_builder.add(lifecycle_provider("mobagen.simulation", "simulation.runtime.v1", {"clock.runtime.v1"}));
    auto registry_result = registry_builder.build();
    REQUIRE(registry_result.ok());
    auto registry = std::move(*registry_result.registry);

    ProductDescriptor product{
        .name = "lifecycle-test",
        .modules = {{.alias = "application", .provider = "default"}},
        .profiles = {{.name = "release", .linkage = LinkageMode::Static, .editor = false}},
    };
    ResolverOptions options{
        .target = TargetPlatform::Windows,
        .profile = "release",
        .aliases = {{.alias = "application", .capability = "application.runtime.v1"}},
        .defaults = {{
            .target = TargetPlatform::Windows,
            .profile = "release",
            .capability = "application.runtime.v1",
            .provider = "mobagen.application",
        }},
    };
    auto resolution_result = resolve_modules(product, registry, options);
    REQUIRE(resolution_result.ok());
    return {std::move(registry), std::move(*resolution_result.resolution)};
  }

  struct ModuleProbe {
    std::string name;
    mobagen::modules::ProviderIndex provider;
    mobagen::modules::CapabilityIndex capability;
    std::vector<std::string>* events{};
    int service{};
    bool fail_configure{};
    bool fail_start{};
    bool throw_on_start{};

    static bool configure(void* opaque, mobagen::modules::ModuleContext& context) {
      auto& probe = *static_cast<ModuleProbe*>(opaque);
      probe.events->push_back(probe.name + ".configure");
      const bool bound = context.bind(probe.capability, probe.provider, &probe.service);
      return bound && !probe.fail_configure;
    }

    static bool start(void* opaque, mobagen::modules::ModuleContext&) {
      auto& probe = *static_cast<ModuleProbe*>(opaque);
      probe.events->push_back(probe.name + ".start");
      if (probe.throw_on_start) throw std::runtime_error("injected start failure");
      return !probe.fail_start;
    }

    static void quiesce(void* opaque, mobagen::modules::ModuleContext&) {
      auto& probe = *static_cast<ModuleProbe*>(opaque);
      probe.events->push_back(probe.name + ".quiesce");
    }

    static void stop(void* opaque, mobagen::modules::ModuleContext&) {
      auto& probe = *static_cast<ModuleProbe*>(opaque);
      probe.events->push_back(probe.name + ".stop");
    }

    static void rollback(void* opaque, mobagen::modules::ModuleContext&) {
      auto& probe = *static_cast<ModuleProbe*>(opaque);
      probe.events->push_back(probe.name + ".rollback");
    }

    mobagen::modules::ModuleLifecycleBinding binding() {
      return {
          .provider = provider,
          .api = {.state = this, .configure = configure, .start = start, .quiesce = quiesce, .stop = stop, .rollback = rollback},
      };
    }
  };

  ModuleProbe make_probe(const LifecycleFixture& fixture, std::string name, std::string provider, std::string capability,
                         std::vector<std::string>& events) {
    const auto provider_index = fixture.registry.find_provider(provider);
    const auto capability_index = fixture.registry.find_capability(capability);
    REQUIRE(provider_index.has_value());
    REQUIRE(capability_index.has_value());
    return {std::move(name), *provider_index, *capability_index, &events};
  }

  std::array<ModuleProbe, 3> make_probes(const LifecycleFixture& fixture, std::vector<std::string>& events) {
    return {
        make_probe(fixture, "clock", "mobagen.clock", "clock.runtime.v1", events),
        make_probe(fixture, "simulation", "mobagen.simulation", "simulation.runtime.v1", events),
        make_probe(fixture, "application", "mobagen.application", "application.runtime.v1", events),
    };
  }

  std::vector<mobagen::modules::ModuleLifecycleBinding> shuffled_bindings(std::array<ModuleProbe, 3>& probes) {
    return {probes[2].binding(), probes[0].binding(), probes[1].binding()};
  }

  bool has_lifecycle_issue(const mobagen::modules::ModuleActivationResult& result, mobagen::modules::ModuleLifecycleIssueCode code,
                           mobagen::modules::ModuleLifecyclePhase phase) {
    return std::ranges::any_of(result.issues, [=](const auto& issue) { return issue.code == code && issue.phase == phase; });
  }

}  // namespace

TEST_CASE("Module lifecycle: activation follows dependency order and shutdown reverses it") {
  using namespace mobagen::modules;

  const auto fixture = make_lifecycle_fixture();
  std::vector<std::string> events;
  auto application = make_probe(fixture, "application", "mobagen.application", "application.runtime.v1", events);
  auto clock = make_probe(fixture, "clock", "mobagen.clock", "clock.runtime.v1", events);
  auto simulation = make_probe(fixture, "simulation", "mobagen.simulation", "simulation.runtime.v1", events);
  const std::vector bindings{application.binding(), clock.binding(), simulation.binding()};

  auto activated = activate_modules(fixture.registry, fixture.resolution, bindings);

  REQUIRE(activated.ok());
  REQUIRE(activated.activation != nullptr);
  CHECK(events
        == std::vector<std::string>{"clock.configure", "simulation.configure", "application.configure", "clock.start", "simulation.start",
                                    "application.start"});
  CHECK(activated.activation->state() == ModuleLifecycleState::Active);
  const auto clock_capability = fixture.registry.find_capability("clock.runtime.v1");
  REQUIRE(clock_capability.has_value());
  REQUIRE(activated.activation->context().binding(*clock_capability) != nullptr);
  CHECK(activated.activation->context().binding(*clock_capability)->service == &clock.service);

  const auto quiesced = activated.activation->quiesce();
  REQUIRE(quiesced.ok());
  CHECK(activated.activation->state() == ModuleLifecycleState::Quiesced);
  CHECK(events
        == std::vector<std::string>{"clock.configure", "simulation.configure", "application.configure", "clock.start", "simulation.start",
                                    "application.start", "application.quiesce", "simulation.quiesce", "clock.quiesce"});

  const auto stopped = activated.activation->stop();
  REQUIRE(stopped.ok());
  CHECK(activated.activation->state() == ModuleLifecycleState::Stopped);
  CHECK(events
        == std::vector<std::string>{"clock.configure", "simulation.configure", "application.configure", "clock.start", "simulation.start",
                                    "application.start", "application.quiesce", "simulation.quiesce", "clock.quiesce", "application.stop",
                                    "simulation.stop", "clock.stop"});

  const auto invalid = activated.activation->quiesce();
  CHECK_FALSE(invalid.ok());
  REQUIRE(invalid.issues.size() == 1);
  CHECK(invalid.issues.front().code == ModuleLifecycleIssueCode::InvalidTransition);
}

TEST_CASE("Module lifecycle: configure failures discard staged bindings and roll back configured providers") {
  using namespace mobagen::modules;

  for (std::size_t fail_at = 0; fail_at < 3; ++fail_at) {
    CAPTURE(fail_at);
    const auto fixture = make_lifecycle_fixture();
    std::vector<std::string> events;
    auto probes = make_probes(fixture, events);
    probes[fail_at].fail_configure = true;
    const auto bindings = shuffled_bindings(probes);

    const auto result = activate_modules(fixture.registry, fixture.resolution, bindings);

    CHECK_FALSE(result.ok());
    CHECK(result.activation == nullptr);
    CHECK(has_lifecycle_issue(result, ModuleLifecycleIssueCode::ConfigureFailed, ModuleLifecyclePhase::Configure));
    std::vector<std::string> expected;
    for (std::size_t index = 0; index <= fail_at; ++index) expected.push_back(probes[index].name + ".configure");
    for (std::size_t index = fail_at; index > 0; --index) expected.push_back(probes[index - 1].name + ".rollback");
    CHECK(events == expected);
  }
}

TEST_CASE("Module lifecycle: start failures stop started providers and roll back configuration in reverse order") {
  using namespace mobagen::modules;

  for (std::size_t fail_at = 0; fail_at < 3; ++fail_at) {
    CAPTURE(fail_at);
    const auto fixture = make_lifecycle_fixture();
    std::vector<std::string> events;
    auto probes = make_probes(fixture, events);
    probes[fail_at].fail_start = true;
    const auto bindings = shuffled_bindings(probes);

    const auto result = activate_modules(fixture.registry, fixture.resolution, bindings);

    CHECK_FALSE(result.ok());
    CHECK(result.activation == nullptr);
    CHECK(has_lifecycle_issue(result, ModuleLifecycleIssueCode::StartFailed, ModuleLifecyclePhase::Start));
    std::vector<std::string> expected{"clock.configure", "simulation.configure", "application.configure"};
    for (std::size_t index = 0; index <= fail_at; ++index) expected.push_back(probes[index].name + ".start");
    for (std::size_t index = fail_at; index > 0; --index) expected.push_back(probes[index - 1].name + ".quiesce");
    for (std::size_t index = fail_at; index > 0; --index) expected.push_back(probes[index - 1].name + ".stop");
    for (std::size_t index = probes.size(); index > 0; --index) expected.push_back(probes[index - 1].name + ".rollback");
    CHECK(events == expected);
  }
}

TEST_CASE("Module lifecycle: callback exceptions stay inside the activation boundary") {
  using namespace mobagen::modules;

  const auto fixture = make_lifecycle_fixture();
  std::vector<std::string> events;
  auto probes = make_probes(fixture, events);
  probes[1].throw_on_start = true;
  const auto bindings = shuffled_bindings(probes);

  const auto result = activate_modules(fixture.registry, fixture.resolution, bindings);

  CHECK_FALSE(result.ok());
  CHECK(has_lifecycle_issue(result, ModuleLifecycleIssueCode::CallbackException, ModuleLifecyclePhase::Start));
  CHECK(events
        == std::vector<std::string>{"clock.configure", "simulation.configure", "application.configure", "clock.start", "simulation.start",
                                    "clock.quiesce", "clock.stop", "application.rollback", "simulation.rollback", "clock.rollback"});
}

TEST_CASE("Module lifecycle: invalid lifecycle tables are rejected before callbacks run") {
  using namespace mobagen::modules;

  const auto fixture = make_lifecycle_fixture();
  std::vector<std::string> events;
  auto probes = make_probes(fixture, events);
  const std::vector bindings{probes[0].binding(), probes[0].binding(), probes[2].binding()};

  const auto result = activate_modules(fixture.registry, fixture.resolution, bindings);

  CHECK_FALSE(result.ok());
  CHECK(result.activation == nullptr);
  CHECK(events.empty());
  CHECK(has_lifecycle_issue(result, ModuleLifecycleIssueCode::InvalidBindings, ModuleLifecyclePhase::Validate));
}
