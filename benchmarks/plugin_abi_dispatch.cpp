#include "benchmark_runner.hpp"

#include "plugins/plugin_activation.hpp"
#include "plugins/plugin_host.hpp"
#include "plugins/plugin_loader.hpp"
#include <mobagen/plugin/runtime_tick_v1.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

  constexpr std::size_t dispatch_batch_size = 1'000'000;
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

  MOBAGEN_NOINLINE MobagenStatus direct_tick(DirectState* state) noexcept {
    if (state == nullptr || state->started == 0) return MOBAGEN_STATUS_CONFLICT;
    ++state->ticks;
    return MOBAGEN_STATUS_OK;
  }

  MOBAGEN_NOINLINE std::uint64_t execute_direct_batch(DirectState* state) {
    unsigned status = 0;
    for (std::size_t invocation = 0; invocation < dispatch_batch_size; ++invocation) {
      status |= static_cast<unsigned>(direct_tick(state));
    }
    if (status != MOBAGEN_STATUS_OK) throw std::runtime_error("direct dispatch failed");
    return state->ticks;
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

    void direct_batch() { observation.fetch_xor(execute_direct_batch(&direct_), std::memory_order_relaxed); }
    void plugin_batch() { observation.fetch_xor(execute_plugin_batch(api_), std::memory_order_relaxed); }

  private:
    DirectState direct_;
    mobagen::plugins::PluginHost host_;
    std::unique_ptr<mobagen::plugins::NativePluginActivation> activation_;
    const MobagenRuntimeTickV1* api_{nullptr};
  };

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = mobagen::benchmark::parse_options(argc, argv);
    PluginAbiDispatchFixture fixture;
    const std::array results{
        mobagen::benchmark::measure("plugin.direct_1m", options, [&fixture] { fixture.direct_batch(); }),
        mobagen::benchmark::measure("plugin.c_abi_1m", options, [&fixture] { fixture.plugin_batch(); }),
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
