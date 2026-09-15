#include "benchmark_frame_v1.h"
#include "benchmark_runner.hpp"

#include "modules/locked_activation_plan.hpp"
#include "native/module_manager.hpp"
#include <mobagen/plugin/runtime_tick_v1.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace benchmark_allocation_probe {
  std::atomic_bool enabled{false};
  std::atomic_size_t count{0};
}  // namespace benchmark_allocation_probe

void* operator new(std::size_t size) {
  if (benchmark_allocation_probe::enabled.load(std::memory_order_relaxed)) {
    benchmark_allocation_probe::count.fetch_add(1, std::memory_order_relaxed);
  }
  if (void* allocation = std::malloc(size == 0 ? 1 : size); allocation != nullptr) {
    return allocation;
  }
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* allocation) noexcept { std::free(allocation); }
void operator delete[](void* allocation) noexcept { ::operator delete(allocation); }
void operator delete(void* allocation, std::size_t) noexcept { std::free(allocation); }
void operator delete[](void* allocation, std::size_t) noexcept { std::free(allocation); }

namespace {

  constexpr std::size_t dispatch_batch_size = 100'000;
  constexpr std::size_t dispatch_interleavings = 20;
  constexpr std::size_t dispatch_operations_per_sample = dispatch_batch_size * dispatch_interleavings;
  constexpr std::size_t frames_per_sample = 240;
  constexpr std::uint32_t frame_item_count = 8'192;
  std::atomic_uint64_t observation{0};

  struct DirectState {
    unsigned started{1};
    std::uint64_t ticks{};
    std::uint64_t frames{};
    std::uint64_t accumulator{UINT64_C(0xcbf29ce484222325)};
  };

  MOBAGEN_BENCHMARK_OPAQUE_CALL MobagenStatus MOBAGEN_PLUGIN_CALL direct_tick(void* opaque) noexcept {
    auto* state = static_cast<DirectState*>(opaque);
    if (state == nullptr || state->started == 0) return MOBAGEN_STATUS_CONFLICT;
    ++state->ticks;
    return MOBAGEN_STATUS_OK;
  }

  MOBAGEN_BENCHMARK_OPAQUE_CALL std::uint64_t MOBAGEN_PLUGIN_CALL direct_tick_count(const void* opaque) noexcept {
    const auto* state = static_cast<const DirectState*>(opaque);
    return state == nullptr ? 0 : state->ticks;
  }

  MOBAGEN_BENCHMARK_OPAQUE_CALL std::uint64_t MOBAGEN_PLUGIN_CALL direct_run_frame(void* opaque, std::uint32_t item_count) noexcept {
    auto* state = static_cast<DirectState*>(opaque);
    if (state == nullptr || state->started == 0) return 0;
    state->accumulator = mobagen_benchmark_frame_workload(state->accumulator ^ state->frames, item_count);
    ++state->frames;
    return state->accumulator;
  }

  MOBAGEN_BENCHMARK_OPAQUE_CALL std::uint64_t MOBAGEN_PLUGIN_CALL direct_frame_count(const void* opaque) noexcept {
    const auto* state = static_cast<const DirectState*>(opaque);
    return state == nullptr ? 0 : state->frames;
  }

  std::unique_ptr<mobagen::modules::LockedPluginActivationPlan> build_plan() {
    using namespace mobagen::modules;
    LockfileDocument document;
    document.metadata.plugins = {{
        .provider = "mobagen.benchmark-abi",
        .version = {1, 0, 0},
        .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
        .package = "benchmark.plugin",
    }};
    document.resolved = {
        {
            .capability = MOBAGEN_RUNTIME_TICK_V1_ID,
            .provider = "mobagen.benchmark-abi",
            .version = {1, 0, 0},
            .linkage = LinkageMode::Dynamic,
        },
        {
            .capability = MOBAGEN_BENCHMARK_FRAME_V1_ID,
            .provider = "mobagen.benchmark-abi",
            .version = {1, 0, 0},
            .linkage = LinkageMode::Dynamic,
        },
    };
    const std::filesystem::path binary{MOBAGEN_ABI_DISPATCH_PLUGIN_PATH};
    const std::array verified{VerifiedLockedPlugin{
        .provider_id = "mobagen.benchmark-abi",
        .version = {1, 0, 0},
        .linkage = LinkageMode::Dynamic,
        .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
        .size = 4096,
        .package_path = binary.parent_path(),
        .binary_path = binary,
    }};
    auto planned = build_locked_plugin_activation_plan(document, verified);
    if (!planned.ok()) throw std::runtime_error("could not build benchmark activation plan");
    return std::move(planned.plan);
  }

  std::unique_ptr<mobagen::compositions::NativeModuleManager> create_manager(std::unique_ptr<mobagen::modules::LockedPluginActivationPlan> plan) {
    auto created = mobagen::compositions::create_native_module_manager(std::move(plan));
    if (!created.ok()) throw std::runtime_error("could not create native module manager");
    return std::move(created.manager);
  }

  std::unique_ptr<mobagen::compositions::NativeModuleManager> create_manager() { return create_manager(build_plan()); }

  mobagen::benchmark::Result measure_manager_startup(const mobagen::benchmark::Options& options) {
    std::vector<std::unique_ptr<mobagen::modules::LockedPluginActivationPlan>> plans;
    plans.reserve(options.warmup + options.samples);
    for (std::size_t index = 0; index < options.warmup + options.samples; ++index) {
      plans.push_back(build_plan());
    }
    std::size_t next = 0;
    return mobagen::benchmark::measure("startup.module_manager", options, [&] {
      auto manager = create_manager(std::move(plans[next++]));
      if (manager->active_count() != 0 || manager->host().size() != 0) {
        throw std::runtime_error("module manager startup eagerly activated plugin code");
      }
    });
  }

  mobagen::benchmark::Result measure_first_acquire(const mobagen::benchmark::Options& options) {
    mobagen::benchmark::Result result{"acquire.first"};
    result.samples_ns.reserve(options.samples);
    for (std::size_t index = 0; index < options.warmup + options.samples; ++index) {
      auto manager = create_manager();
      const auto begin = std::chrono::steady_clock::now();
      auto acquired = manager->acquire(MOBAGEN_BENCHMARK_FRAME_V1_ID, 1);
      const auto end = std::chrono::steady_clock::now();
      if (!acquired.ok() || manager->active_count() != 1) {
        throw std::runtime_error("first capability acquire failed");
      }
      if (!manager->stop().ok()) throw std::runtime_error("benchmark plugin stop failed");
      if (index >= options.warmup) {
        result.samples_ns.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
      }
    }
    result.median_ns = mobagen::benchmark::percentile(result.samples_ns, 0.50);
    result.p95_ns = mobagen::benchmark::percentile(result.samples_ns, 0.95);
    return result;
  }

  MOBAGEN_BENCHMARK_OPAQUE_CALL std::uint64_t execute_tick_batch(const MobagenRuntimeTickV1* api) {
    unsigned status = 0;
    for (std::size_t invocation = 0; invocation < dispatch_batch_size; ++invocation) {
      status |= static_cast<unsigned>(api->tick(api->plugin_state));
    }
    if (status != MOBAGEN_STATUS_OK) throw std::runtime_error("tick batch failed");
    return api->tick_count(api->plugin_state);
  }

  MOBAGEN_BENCHMARK_OPAQUE_CALL std::uint64_t execute_frame_batch(const MobagenBenchmarkFrameV1* api) {
    const auto result = api->run_frame(api->plugin_state, frame_item_count);
    if (api->frame_count(api->plugin_state) == 0) {
      throw std::runtime_error("frame batch failed");
    }
    return result;
  }

  class RuntimeFixture {
  public:
    RuntimeFixture() : manager_(create_manager()) {
      auto tick = manager_->acquire(MOBAGEN_RUNTIME_TICK_V1_ID, 1);
      auto frame = manager_->acquire(MOBAGEN_BENCHMARK_FRAME_V1_ID, 1);
      if (!tick.ok() || !frame.ok()) {
        throw std::runtime_error("could not acquire benchmark capabilities");
      }
      plugin_tick_ = static_cast<const MobagenRuntimeTickV1*>(tick.binding->function_table);
      plugin_frame_ = static_cast<const MobagenBenchmarkFrameV1*>(frame.binding->function_table);
      direct_tick_api_ = {
          .header = {MOBAGEN_RUNTIME_TICK_V1_SIZE, 1},
          .plugin_state = &direct_state_,
          .tick = direct_tick,
          .tick_count = direct_tick_count,
      };
      direct_frame_api_ = {
          .header = {MOBAGEN_BENCHMARK_FRAME_V1_SIZE, 1},
          .plugin_state = &direct_state_,
          .run_frame = direct_run_frame,
          .frame_count = direct_frame_count,
      };
    }

    ~RuntimeFixture() {
      if (manager_ != nullptr) (void)manager_->stop();
    }

    void direct_dispatch() { observation.fetch_xor(execute_tick_batch(&direct_tick_api_), std::memory_order_relaxed); }
    void modular_dispatch() { observation.fetch_xor(execute_tick_batch(plugin_tick_), std::memory_order_relaxed); }
    void direct_frame() { observation.fetch_xor(execute_frame_batch(&direct_frame_api_), std::memory_order_relaxed); }
    void modular_frame() { observation.fetch_xor(execute_frame_batch(plugin_frame_), std::memory_order_relaxed); }

  private:
    DirectState direct_state_;
    MobagenRuntimeTickV1 direct_tick_api_{};
    MobagenBenchmarkFrameV1 direct_frame_api_{};
    std::unique_ptr<mobagen::compositions::NativeModuleManager> manager_;
    const MobagenRuntimeTickV1* plugin_tick_{};
    const MobagenBenchmarkFrameV1* plugin_frame_{};
  };

  bool gate_overhead(std::string_view label, const mobagen::benchmark::PairedResult& result, double limit) {
    const auto overhead = mobagen::benchmark::paired_overhead(result);
    std::cerr << label << " overhead: median " << overhead.median_percent << "%, p05 " << overhead.p05_percent << "% (limit " << limit << "%)\n";
    return overhead.p05_percent <= limit;
  }

  bool gate_dispatch_overhead(std::string_view label, const mobagen::benchmark::PairedResult& result, double limit) {
    const auto overhead = mobagen::benchmark::paired_operation_overhead(result, dispatch_operations_per_sample);
    std::cerr << label << " overhead: median " << overhead.median_ns << " ns/call, p05 " << overhead.p05_ns << " ns/call (limit " << limit
              << " ns/call)\n";
    return overhead.p05_ns <= limit;
  }

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = mobagen::benchmark::parse_options(argc, argv);
    const auto startup = measure_manager_startup(options);
    const auto first_acquire = measure_first_acquire(options);
    RuntimeFixture fixture;
    const auto dispatch = mobagen::benchmark::measure_paired(
        "dispatch.direct_2m", "dispatch.cached_2m", options, [&] { fixture.direct_dispatch(); }, [&] { fixture.modular_dispatch(); },
        dispatch_interleavings);
    const auto frame = mobagen::benchmark::measure_paired(
        "frame.direct", "frame.modular", options, [&] { fixture.direct_frame(); }, [&] { fixture.modular_frame(); }, frames_per_sample);
    const std::array results{
        startup, first_acquire, dispatch.baseline, dispatch.candidate, frame.baseline, frame.candidate,
    };
    mobagen::benchmark::write_json(std::cout, options, results);

    benchmark_allocation_probe::count.store(0, std::memory_order_relaxed);
    benchmark_allocation_probe::enabled.store(true, std::memory_order_release);
    for (std::size_t index = 0; index < dispatch_interleavings; ++index) {
      fixture.modular_dispatch();
    }
    for (std::size_t index = 0; index < frames_per_sample; ++index) {
      fixture.modular_frame();
    }
    benchmark_allocation_probe::enabled.store(false, std::memory_order_release);
    const auto allocations = benchmark_allocation_probe::count.load(std::memory_order_relaxed);
    std::cerr << "warmed modular allocations: " << allocations << '\n';
    if (allocations != 0) {
      std::cerr << "warmed modular paths allocated " << allocations << " times\n";
      return 4;
    }

    const auto relative_dispatch = mobagen::benchmark::paired_overhead(dispatch);
    std::cerr << "warmed dispatch relative overhead (diagnostic): median " << relative_dispatch.median_percent << "%, p05 "
              << relative_dispatch.p05_percent << "%\n";
    const bool dispatch_ok
        = !options.max_dispatch_overhead_ns.has_value() || gate_dispatch_overhead("warmed dispatch", dispatch, *options.max_dispatch_overhead_ns);
    const bool frame_ok = !options.max_overhead_percent.has_value() || gate_overhead("representative frame", frame, *options.max_overhead_percent);
    if (!dispatch_ok || !frame_ok) return 3;
    return 0;
  } catch (const std::invalid_argument& error) {
    std::cerr << "invalid benchmark arguments: " << error.what() << '\n';
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "native module benchmark failed: " << error.what() << '\n';
    return 1;
  }
}
