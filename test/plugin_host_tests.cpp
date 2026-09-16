#include <doctest/doctest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <thread>

#include "plugins/plugin_host.hpp"

namespace module_allocation_probe {
  extern std::atomic_bool enabled;
  extern std::atomic_size_t count;
}  // namespace module_allocation_probe

namespace {

  MobagenStringView view(std::string_view value) { return {value.data(), value.size()}; }

  struct TickState {
    std::uint64_t ticks{0};
  };

  struct TickApiV1 {
    MobagenCapabilityHeaderV1 header;
    void* state;
    void(MOBAGEN_PLUGIN_CALL* tick)(void*);
  };

  void MOBAGEN_PLUGIN_CALL tick(void* opaque) { ++static_cast<TickState*>(opaque)->ticks; }

}  // namespace

TEST_CASE("Plugin host: capability publication is transactional and directly freezable") {
  using namespace mobagen::plugins;
  PluginHost host;
  TickState state;
  const TickApiV1 service{{sizeof(TickApiV1), 1}, &state, tick};

  REQUIRE(host.begin_registration("customer.clock"));
  CHECK(host.api().publish_capability(host.api().host_context, view("runtime.tick.v1"), 1, &service, sizeof(service)) == MOBAGEN_STATUS_OK);
  CHECK_FALSE(host.find<TickApiV1>("runtime.tick.v1", 1).has_value());
  REQUIRE(host.commit_registration());

  const auto frozen = host.find<TickApiV1>("runtime.tick.v1", 1);
  REQUIRE(frozen.has_value());
  CHECK(*frozen == &service);

  module_allocation_probe::count.store(0, std::memory_order_relaxed);
  module_allocation_probe::enabled.store(true, std::memory_order_relaxed);
  for (std::size_t invocation = 0; invocation < 100'000; ++invocation) {
    (*frozen)->tick((*frozen)->state);
  }
  module_allocation_probe::enabled.store(false, std::memory_order_relaxed);
  CHECK(module_allocation_probe::count.load(std::memory_order_relaxed) == 0);
  CHECK(state.ticks == 100'000);
}

TEST_CASE("Plugin host: rollback duplicate and ABI checks preserve committed services") {
  using namespace mobagen::plugins;
  PluginHost host;
  TickState first_state;
  TickState second_state;
  const TickApiV1 first{{sizeof(TickApiV1), 1}, &first_state, tick};
  const TickApiV1 second{{sizeof(TickApiV1), 1}, &second_state, tick};

  CHECK(host.api().publish_capability(host.api().host_context, view("runtime.tick.v1"), 1, &first, sizeof(first)) == MOBAGEN_STATUS_CONFLICT);
  REQUIRE(host.begin_registration("customer.first"));
  REQUIRE(host.api().publish_capability(host.api().host_context, view("runtime.tick.v1"), 1, &first, sizeof(first)) == MOBAGEN_STATUS_OK);
  host.rollback_registration();
  CHECK_FALSE(host.find<TickApiV1>("runtime.tick.v1", 1).has_value());

  REQUIRE(host.begin_registration("customer.first"));
  REQUIRE(host.api().publish_capability(host.api().host_context, view("runtime.tick.v1"), 1, &first, sizeof(first)) == MOBAGEN_STATUS_OK);
  REQUIRE(host.commit_registration());
  REQUIRE(host.begin_registration("customer.second"));
  CHECK(host.api().publish_capability(host.api().host_context, view("runtime.tick.v1"), 1, &second, sizeof(second)) == MOBAGEN_STATUS_CONFLICT);
  host.rollback_registration();

  CHECK_FALSE(host.find<TickApiV1>("runtime.tick.v1", 2).has_value());
  const auto current = host.find<TickApiV1>("runtime.tick.v1", 1);
  REQUIRE(current.has_value());
  CHECK(*current == &first);
  CHECK(host.remove_provider("customer.first"));
  CHECK_FALSE(host.find<TickApiV1>("runtime.tick.v1", 1).has_value());
}

TEST_CASE("Plugin host: C lookup initializes outputs and reports version mismatch") {
  using namespace mobagen::plugins;
  PluginHost host;
  TickState state;
  const TickApiV1 service{{sizeof(TickApiV1), 3}, &state, tick};
  REQUIRE(host.begin_registration("customer.clock"));
  REQUIRE(host.api().publish_capability(host.api().host_context, view("runtime.tick.v1"), 3, &service, sizeof(service)) == MOBAGEN_STATUS_OK);
  REQUIRE(host.commit_registration());

  int sentinel = 0;
  const void* table = &sentinel;
  std::uint32_t size = 99;
  CHECK(host.api().find_capability(host.api().host_context, view("runtime.tick.v1"), 4, &table, &size) == MOBAGEN_STATUS_UNSUPPORTED);
  CHECK(table == nullptr);
  CHECK(size == 0);
  CHECK(host.api().find_capability(host.api().host_context, view("runtime.tick.v1"), 3, &table, &size) == MOBAGEN_STATUS_OK);
  CHECK(table == &service);
  CHECK(size == sizeof(service));
}

TEST_CASE("Plugin host: published function tables must carry a matching bounded header") {
  using namespace mobagen::plugins;
  PluginHost host;
  TickState state;
  TickApiV1 service{{sizeof(TickApiV1), 1}, &state, tick};
  REQUIRE(host.begin_registration("customer.clock"));

  service.header.abi_version = 2;
  CHECK(host.api().publish_capability(host.api().host_context, view("runtime.tick.v1"), 1, &service, sizeof(service)) == MOBAGEN_STATUS_UNSUPPORTED);
  service.header.abi_version = 1;
  service.header.struct_size = sizeof(MobagenCapabilityHeaderV1) - 1;
  CHECK(host.api().publish_capability(host.api().host_context, view("runtime.tick.v1"), 1, &service, sizeof(service)) == MOBAGEN_STATUS_UNSUPPORTED);
  service.header.struct_size = sizeof(service) + 1;
  CHECK(host.api().publish_capability(host.api().host_context, view("runtime.tick.v1"), 1, &service, sizeof(service)) == MOBAGEN_STATUS_UNSUPPORTED);
  CHECK(host.api().publish_capability(host.api().host_context, view("runtime.tick.v1"), 1, &service, sizeof(MobagenCapabilityHeaderV1) - 1)
        == MOBAGEN_STATUS_INVALID_ARGUMENT);

  host.rollback_registration();
  CHECK(host.size() == 0);
}

TEST_CASE("Plugin host: aligned allocator and owner thread checks are explicit") {
  using namespace mobagen::plugins;
  PluginHost host;
  void* memory = host.api().allocate(host.api().host_context, 128, 64);
  REQUIRE(memory != nullptr);
  CHECK(reinterpret_cast<std::uintptr_t>(memory) % 64 == 0);
  host.api().deallocate(host.api().host_context, memory, 128, 64);
  CHECK(host.api().allocate(host.api().host_context, 8, 3) == nullptr);

  bool began_off_thread = true;
  std::thread worker([&] { began_off_thread = host.begin_registration("customer.worker"); });
  worker.join();
  CHECK_FALSE(began_off_thread);
}
