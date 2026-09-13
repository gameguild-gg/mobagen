#include "benchmark_runner.hpp"

#include "modules/execution_graph.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

  constexpr std::size_t dispatch_batch_size = 100'000;
  std::atomic_uint64_t observation{0};

#ifdef _MSC_VER
#  define MOBAGEN_NOINLINE __declspec(noinline)
#else
#  define MOBAGEN_NOINLINE __attribute__((noinline))
#endif

  struct DispatchService {
    std::uint64_t ticks{};

    void tick(std::uint64_t invocation) noexcept { ticks = (ticks ^ invocation) * 6364136223846793005ULL + 1442695040888963407ULL; }
  };

  MOBAGEN_NOINLINE std::uint64_t execute_dispatch_batch(DispatchService* service) noexcept {
    for (std::size_t invocation = 0; invocation < dispatch_batch_size; ++invocation) service->tick(invocation);
    return service->ticks;
  }

  struct ModuleState {
    mobagen::modules::ProviderIndex provider;
    mobagen::modules::CapabilityIndex capability;
    DispatchService service;

    static bool configure(void* opaque, mobagen::modules::ModuleContext& context) {
      auto& state = *static_cast<ModuleState*>(opaque);
      return context.bind(state.capability, state.provider, &state.service);
    }
  };

  struct ResolvedKernel {
    mobagen::modules::CapabilityRegistry registry;
    mobagen::modules::ModuleResolution resolution;
  };

  ResolvedKernel build_kernel() {
    using namespace mobagen::modules;

    CapabilityRegistryBuilder registry_builder;
    registry_builder.add({
        .id = "mobagen.benchmark.dispatch",
        .version = {1, 0, 0},
        .provides = {"benchmark.dispatch.v1"},
        .targets = {TargetPlatform::Windows, TargetPlatform::Linux, TargetPlatform::MacOS},
        .linkages = {LinkageMode::Static},
    });
    auto registry_result = registry_builder.build();
    if (!registry_result.ok()) throw std::runtime_error("could not build dispatch benchmark registry");
    auto registry = std::move(*registry_result.registry);

    ProductDescriptor product{
        .name = "dispatch-benchmark",
        .modules = {{.alias = "dispatch", .provider = "mobagen.benchmark.dispatch"}},
        .profiles = {{.name = "release", .linkage = LinkageMode::Static, .editor = false}},
    };
#ifdef _WIN32
    constexpr auto target = TargetPlatform::Windows;
#elif defined(__APPLE__)
    constexpr auto target = TargetPlatform::MacOS;
#else
    constexpr auto target = TargetPlatform::Linux;
#endif
    ResolverOptions options{
        .target = target,
        .profile = "release",
        .aliases = {{.alias = "dispatch", .capability = "benchmark.dispatch.v1"}},
    };
    auto resolution_result = resolve_modules(product, registry, options);
    if (!resolution_result.ok()) throw std::runtime_error("could not resolve dispatch benchmark graph");
    return {std::move(registry), std::move(*resolution_result.resolution)};
  }

  class DispatchFixture {
  public:
    DispatchFixture() : kernel_(build_kernel()) {
      const auto provider = kernel_.registry.find_provider("mobagen.benchmark.dispatch");
      const auto capability = kernel_.registry.find_capability("benchmark.dispatch.v1");
      if (!provider.has_value() || !capability.has_value()) throw std::runtime_error("dispatch benchmark indices are unavailable");
      state_.provider = *provider;
      state_.capability = *capability;

      const std::array bindings{mobagen::modules::ModuleLifecycleBinding{
          .provider = *provider,
          .api = {.state = &state_, .configure = ModuleState::configure},
      }};
      activation_ = mobagen::modules::activate_modules(kernel_.registry, kernel_.resolution, bindings);
      if (!activation_.ok()) throw std::runtime_error("could not activate dispatch benchmark graph");
      graph_ = mobagen::modules::freeze_execution_graph(kernel_.registry, kernel_.resolution, *activation_.activation);
      if (!graph_.ok()) throw std::runtime_error("could not freeze dispatch benchmark graph");
      handle_ = graph_.graph->handle<DispatchService>(*capability);
      if (!handle_.has_value()) throw std::runtime_error("dispatch benchmark handle is unavailable");
    }

    ~DispatchFixture() {
      if (activation_.activation == nullptr) return;
      if (activation_.activation->state() == mobagen::modules::ModuleLifecycleState::Active) (void)activation_.activation->quiesce();
      if (activation_.activation->state() == mobagen::modules::ModuleLifecycleState::Quiesced) (void)activation_.activation->stop();
    }

    void direct_batch() { observation.fetch_xor(execute_dispatch_batch(&state_.service), std::memory_order_relaxed); }

    void frozen_batch() {
      auto* service = graph_.graph->service(*handle_);
      if (service == nullptr) throw std::runtime_error("dispatch benchmark handle became stale");
      observation.fetch_xor(execute_dispatch_batch(service), std::memory_order_relaxed);
    }

  private:
    ResolvedKernel kernel_;
    ModuleState state_;
    mobagen::modules::ModuleActivationResult activation_;
    mobagen::modules::ExecutionGraphResult graph_;
    std::optional<mobagen::modules::CapabilityHandle<DispatchService>> handle_;
  };

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = mobagen::benchmark::parse_options(argc, argv);
    DispatchFixture fixture;
    const std::array results{
        mobagen::benchmark::measure("module.direct_batch", options, [&fixture] { fixture.direct_batch(); }),
        mobagen::benchmark::measure("module.frozen_batch", options, [&fixture] { fixture.frozen_batch(); }),
    };
    mobagen::benchmark::write_json(std::cout, options, results);
    return 0;
  } catch (const std::invalid_argument& error) {
    std::cerr << "invalid benchmark arguments: " << error.what() << '\n';
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "benchmark failed: " << error.what() << '\n';
    return 1;
  }
}
