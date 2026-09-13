#include <doctest/doctest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "modules/execution_graph.hpp"

namespace module_allocation_probe {
  std::atomic_bool enabled{false};
  std::atomic_size_t count{0};
}  // namespace module_allocation_probe

void* operator new(std::size_t size) {
  if (module_allocation_probe::enabled.load(std::memory_order_relaxed)) {
    module_allocation_probe::count.fetch_add(1, std::memory_order_relaxed);
  }
  if (void* allocation = std::malloc(size == 0 ? 1 : size); allocation != nullptr) return allocation;
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void* allocation) noexcept { std::free(allocation); }

void operator delete[](void* allocation) noexcept { ::operator delete(allocation); }

void operator delete(void* allocation, std::size_t) noexcept { std::free(allocation); }

void operator delete[](void* allocation, std::size_t) noexcept { std::free(allocation); }

namespace {

  struct TickService {
    std::uint64_t ticks{};

    void tick() noexcept { ++ticks; }
  };

  struct ExecutionModule {
    mobagen::modules::ProviderIndex provider;
    mobagen::modules::CapabilityIndex capability;
    TickService service;

    static bool configure(void* opaque, mobagen::modules::ModuleContext& context) {
      auto& module = *static_cast<ExecutionModule*>(opaque);
      return context.bind(module.capability, module.provider, &module.service);
    }

    mobagen::modules::ModuleLifecycleBinding binding() { return {.provider = provider, .api = {.state = this, .configure = configure}}; }
  };

  struct ExecutionFixture {
    mobagen::modules::CapabilityRegistry registry;
    mobagen::modules::ModuleResolution resolution;
  };

  mobagen::modules::ProviderDescriptor execution_provider(std::string id, std::string capability, std::vector<std::string> required = {}) {
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

  ExecutionFixture make_execution_fixture() {
    using namespace mobagen::modules;

    CapabilityRegistryBuilder builder;
    builder.add(execution_provider("mobagen.clock", "clock.runtime.v1"));
    builder.add(execution_provider("mobagen.diagnostics", "diagnostics.runtime.v1"));
    builder.add(execution_provider("mobagen.simulation", "simulation.runtime.v1", {"clock.runtime.v1"}));
    auto registry_result = builder.build();
    REQUIRE(registry_result.ok());
    auto registry = std::move(*registry_result.registry);

    ProductDescriptor product{
        .name = "execution-graph-test",
        .modules = {{.alias = "simulation", .provider = "default"}},
        .profiles = {{.name = "release", .linkage = LinkageMode::Static, .editor = false}},
    };
    ResolverOptions options{
        .target = TargetPlatform::Windows,
        .profile = "release",
        .aliases = {{.alias = "simulation", .capability = "simulation.runtime.v1"}},
        .defaults = {{
            .target = TargetPlatform::Windows,
            .profile = "release",
            .capability = "simulation.runtime.v1",
            .provider = "mobagen.simulation",
        }},
    };
    auto resolution_result = resolve_modules(product, registry, options);
    REQUIRE(resolution_result.ok());
    return {std::move(registry), std::move(*resolution_result.resolution)};
  }

  ExecutionModule make_execution_module(const ExecutionFixture& fixture, std::string_view provider, std::string_view capability) {
    const auto provider_index = fixture.registry.find_provider(provider);
    const auto capability_index = fixture.registry.find_capability(capability);
    REQUIRE(provider_index.has_value());
    REQUIRE(capability_index.has_value());
    return {*provider_index, *capability_index};
  }

  struct ActiveExecutionFixture {
    std::unique_ptr<ExecutionModule> clock;
    std::unique_ptr<ExecutionModule> simulation;
    mobagen::modules::ModuleActivationResult activation;
  };

  ActiveExecutionFixture activate_execution_fixture(const ExecutionFixture& fixture) {
    auto clock = std::make_unique<ExecutionModule>(make_execution_module(fixture, "mobagen.clock", "clock.runtime.v1"));
    auto simulation = std::make_unique<ExecutionModule>(make_execution_module(fixture, "mobagen.simulation", "simulation.runtime.v1"));
    const std::vector bindings{simulation->binding(), clock->binding()};
    auto activation = mobagen::modules::activate_modules(fixture.registry, fixture.resolution, bindings);
    REQUIRE(activation.ok());
    return {std::move(clock), std::move(simulation), std::move(activation)};
  }

}  // namespace

TEST_CASE("Module execution graph: freeze materializes immutable compact dispatch slots") {
  using namespace mobagen::modules;

  static_assert(!std::is_copy_constructible_v<ExecutionGraph>);
  static_assert(std::is_same_v<decltype(std::declval<const ExecutionGraph&>().slots()), std::span<const ExecutionSlot>>);

  const auto fixture = make_execution_fixture();
  auto active = activate_execution_fixture(fixture);
  auto frozen = freeze_execution_graph(fixture.registry, fixture.resolution, *active.activation.activation);

  REQUIRE(frozen.ok());
  REQUIRE(frozen.graph.has_value());
  CHECK(frozen.graph->generation() == active.activation.activation->generation());
  CHECK(frozen.graph->slots().size() == fixture.registry.capability_count());
  CHECK(frozen.graph->bound_slot_count() == fixture.resolution.selections().size());

  const auto clock_capability = fixture.registry.find_capability("clock.runtime.v1");
  const auto diagnostics_capability = fixture.registry.find_capability("diagnostics.runtime.v1");
  REQUIRE(clock_capability.has_value());
  REQUIRE(diagnostics_capability.has_value());
  const auto clock_handle = frozen.graph->handle<TickService>(*clock_capability);
  REQUIRE(clock_handle.has_value());
  CHECK_FALSE(frozen.graph->handle<TickService>(*diagnostics_capability).has_value());
  auto* clock = frozen.graph->service(*clock_handle);
  REQUIRE(clock != nullptr);
  clock->tick();
  CHECK(active.clock->service.ticks == 1);

  REQUIRE(active.activation.activation->quiesce().ok());
  REQUIRE(active.activation.activation->stop().ok());
}

TEST_CASE("Module execution graph: handles from another activation generation are rejected") {
  using namespace mobagen::modules;

  const auto fixture = make_execution_fixture();
  auto first = activate_execution_fixture(fixture);
  auto second = activate_execution_fixture(fixture);
  auto first_graph = freeze_execution_graph(fixture.registry, fixture.resolution, *first.activation.activation);
  auto second_graph = freeze_execution_graph(fixture.registry, fixture.resolution, *second.activation.activation);
  REQUIRE(first_graph.ok());
  REQUIRE(second_graph.ok());

  const auto capability = fixture.registry.find_capability("clock.runtime.v1");
  REQUIRE(capability.has_value());
  const auto first_handle = first_graph.graph->handle<TickService>(*capability);
  const auto second_handle = second_graph.graph->handle<TickService>(*capability);
  REQUIRE(first_handle.has_value());
  REQUIRE(second_handle.has_value());
  CHECK(first_graph.graph->service(*first_handle) == &first.clock->service);
  CHECK(second_graph.graph->service(*second_handle) == &second.clock->service);
  CHECK(second_graph.graph->service(*first_handle) == nullptr);
  CHECK(first_graph.graph->service(*second_handle) == nullptr);

  REQUIRE(first.activation.activation->quiesce().ok());
  REQUIRE(first.activation.activation->stop().ok());
  REQUIRE(second.activation.activation->quiesce().ok());
  REQUIRE(second.activation.activation->stop().ok());
}

TEST_CASE("Module execution graph: only active module graphs can be frozen") {
  using namespace mobagen::modules;

  const auto fixture = make_execution_fixture();
  auto active = activate_execution_fixture(fixture);
  REQUIRE(active.activation.activation->quiesce().ok());

  const auto frozen = freeze_execution_graph(fixture.registry, fixture.resolution, *active.activation.activation);

  CHECK_FALSE(frozen.ok());
  CHECK_FALSE(frozen.graph.has_value());
  REQUIRE(frozen.issues.size() == 1);
  CHECK(frozen.issues.front().code == ExecutionGraphIssueCode::InactiveActivation);
  REQUIRE(active.activation.activation->stop().ok());
}

TEST_CASE("Module execution graph: warmed compact dispatch performs zero allocations") {
  using namespace mobagen::modules;

  const auto fixture = make_execution_fixture();
  auto active = activate_execution_fixture(fixture);
  auto frozen = freeze_execution_graph(fixture.registry, fixture.resolution, *active.activation.activation);
  REQUIRE(frozen.ok());
  const auto capability = fixture.registry.find_capability("clock.runtime.v1");
  REQUIRE(capability.has_value());
  const auto handle = frozen.graph->handle<TickService>(*capability);
  REQUIRE(handle.has_value());
  REQUIRE(frozen.graph->service(*handle) != nullptr);

  module_allocation_probe::count.store(0, std::memory_order_relaxed);
  module_allocation_probe::enabled.store(true, std::memory_order_relaxed);
  for (std::size_t invocation = 0; invocation < 100'000; ++invocation) {
    frozen.graph->service(*handle)->tick();
  }
  module_allocation_probe::enabled.store(false, std::memory_order_relaxed);
  const auto allocations = module_allocation_probe::count.load(std::memory_order_relaxed);

  CHECK(allocations == 0);
  CHECK(active.clock->service.ticks == 100'000);
  REQUIRE(active.activation.activation->quiesce().ok());
  REQUIRE(active.activation.activation->stop().ok());
}
