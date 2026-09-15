#include "benchmark_runner.hpp"

#include "plugins/plugin_activation.hpp"
#include "plugins/plugin_host.hpp"
#include "plugins/plugin_loader.hpp"
#include <mobagen/plugin/runtime_tick_v1.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

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
  std::atomic_uint64_t observation{0};

#ifdef _MSC_VER
#  define MOBAGEN_NOINLINE __declspec(noinline)
#else
#  define MOBAGEN_NOINLINE __attribute__((noinline))
#endif

  struct DirectState {
    unsigned started{1};
    std::uint64_t ticks{};
  };

  MOBAGEN_NOINLINE MobagenStatus MOBAGEN_PLUGIN_CALL direct_tick(void* opaque) noexcept {
    auto* state = static_cast<DirectState*>(opaque);
    if (state == nullptr || state->started == 0) return MOBAGEN_STATUS_CONFLICT;
    ++state->ticks;
    return MOBAGEN_STATUS_OK;
  }

  MOBAGEN_NOINLINE std::uint64_t MOBAGEN_PLUGIN_CALL direct_tick_count(const void* opaque) noexcept {
    const auto* state = static_cast<const DirectState*>(opaque);
    return state == nullptr ? 0 : state->ticks;
  }

  MOBAGEN_NOINLINE std::uint64_t execute_plugin_batch(const MobagenRuntimeTickV1* api) {
    unsigned status = 0;
    for (std::size_t invocation = 0; invocation < dispatch_batch_size; ++invocation) {
      status |= static_cast<unsigned>(api->tick(api->plugin_state));
    }
    if (status != MOBAGEN_STATUS_OK) throw std::runtime_error("plugin C ABI dispatch failed");
    return api->tick_count(api->plugin_state);
  }

  class PluginAbiDispatchFixture {
  public:
    PluginAbiDispatchFixture() {
      direct_api_ = {
          .header = {MOBAGEN_RUNTIME_TICK_V1_SIZE, 1},
          .plugin_state = &direct_,
          .tick = direct_tick,
          .tick_count = direct_tick_count,
      };
      auto loaded = mobagen::plugins::load_native_plugin_binary(MOBAGEN_ABI_DISPATCH_PLUGIN_PATH, host_.api());
      if (!loaded.plugin.has_value()) throw std::runtime_error("could not load plugin ABI benchmark library");
      auto activated = mobagen::plugins::activate_loaded_native_plugin(std::move(*loaded.plugin), host_);
      if (!activated.ok()) throw std::runtime_error("could not activate plugin ABI benchmark library");
      activation_ = std::move(activated.activation);
      const auto api = host_.find<MobagenRuntimeTickV1>(MOBAGEN_RUNTIME_TICK_V1_ID, 1);
      if (!api.has_value()) throw std::runtime_error("plugin ABI benchmark capability was not published");
      api_ = *api;
    }

    ~PluginAbiDispatchFixture() {
      if (activation_ == nullptr) return;
      if (activation_->state() == mobagen::plugins::NativePluginActivationState::Active) (void)activation_->quiesce();
      if (activation_->state() == mobagen::plugins::NativePluginActivationState::Quiesced) (void)activation_->stop();
    }

    void direct_batch() { observation.fetch_xor(execute_plugin_batch(&direct_api_), std::memory_order_relaxed); }
    void plugin_batch() { observation.fetch_xor(execute_plugin_batch(api_), std::memory_order_relaxed); }

  private:
    DirectState direct_;
    MobagenRuntimeTickV1 direct_api_{};
    mobagen::plugins::PluginHost host_;
    std::unique_ptr<mobagen::plugins::NativePluginActivation> activation_;
    const MobagenRuntimeTickV1* api_{nullptr};
  };

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = mobagen::benchmark::parse_options(argc, argv);
    PluginAbiDispatchFixture fixture;
    const auto paired = mobagen::benchmark::measure_paired(
        "plugin.direct_2m", "plugin.c_abi_2m", options, [&fixture] { fixture.direct_batch(); }, [&fixture] { fixture.plugin_batch(); },
        dispatch_interleavings);
    const std::array results{paired.baseline, paired.candidate};
    mobagen::benchmark::write_json(std::cout, options, results);
    benchmark_allocation_probe::count.store(0, std::memory_order_relaxed);
    benchmark_allocation_probe::enabled.store(true, std::memory_order_release);
    for (std::size_t index = 0; index < dispatch_interleavings; ++index) {
      fixture.plugin_batch();
    }
    benchmark_allocation_probe::enabled.store(false, std::memory_order_release);
    const auto allocations = benchmark_allocation_probe::count.load(std::memory_order_relaxed);
    std::cerr << "warmed plugin dispatch allocations: " << allocations << '\n';
    if (allocations != 0) {
      std::cerr << "warmed plugin dispatch allocated " << allocations << " times\n";
      return 4;
    }
    if (options.max_overhead_percent.has_value()) {
      const auto overhead = mobagen::benchmark::paired_overhead(paired);
      std::cerr << "warmed dispatch overhead: median " << overhead.median_percent << "%, p05 " << overhead.p05_percent << "% (limit "
                << *options.max_overhead_percent << "%)\n";
      if (overhead.p05_percent > *options.max_overhead_percent) return 3;
    }
    return 0;
  } catch (const std::invalid_argument& error) {
    std::cerr << "invalid benchmark arguments: " << error.what() << '\n';
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "benchmark failed: " << error.what() << '\n';
    return 1;
  }
}
