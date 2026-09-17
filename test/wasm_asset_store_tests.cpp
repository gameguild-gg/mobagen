#include <doctest/doctest.h>

#include "plugins/wasm_asset_store.hpp"
#include "plugins/wasm_runtime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace module_allocation_probe {
  extern std::atomic_bool enabled;
  extern std::atomic_size_t count;
}  // namespace module_allocation_probe

extern "C" {
std::uint32_t mobagen_wasm_plugin_allocate_v1(std::uint32_t, std::uint32_t);
std::uint32_t mobagen_wasm_plugin_deallocate_v1(std::uint32_t, std::uint32_t, std::uint32_t);
std::uint32_t mobagen_wasm_plugin_query_v1(std::uint32_t, std::uint32_t);
std::uint32_t mobagen_wasm_plugin_configure_v1(std::uint32_t, std::uint32_t);
std::uint32_t mobagen_wasm_plugin_start_v1(void);
std::uint32_t mobagen_wasm_plugin_quiesce_v1(void);
std::uint32_t mobagen_wasm_plugin_stop_v1(void);
std::uint32_t mobagen_wasm_plugin_process_v1(std::uint32_t, std::uint32_t, std::uint32_t);
std::uint8_t* mobagen_wasm_asset_store_test_memory_v1(void);
std::uint32_t mobagen_wasm_asset_store_test_memory_size_v1(void);
void mobagen_wasm_asset_store_test_reset_v1(void);
}

namespace {

  struct ProviderCalls {
    std::uint32_t process{};
  };

  class PortableAssetStoreInstance final : public mobagen::plugins::PortableWasmInstance {
  public:
    explicit PortableAssetStoreInstance(std::shared_ptr<ProviderCalls> calls) : calls_(std::move(calls)) { mobagen_wasm_asset_store_test_reset_v1(); }

    mobagen::plugins::WasmInvocationResult invoke(mobagen::plugins::WasmPluginExport function, std::span<const std::uint32_t> arguments) override {
      using mobagen::plugins::WasmInvocationResult;
      switch (function) {
        case mobagen::plugins::WasmPluginExport::Allocate:
          return WasmInvocationResult::success(mobagen_wasm_plugin_allocate_v1(arguments[0], arguments[1]));
        case mobagen::plugins::WasmPluginExport::Deallocate:
          return WasmInvocationResult::success(mobagen_wasm_plugin_deallocate_v1(arguments[0], arguments[1], arguments[2]));
        case mobagen::plugins::WasmPluginExport::Query:
          return WasmInvocationResult::success(mobagen_wasm_plugin_query_v1(arguments[0], arguments[1]));
        case mobagen::plugins::WasmPluginExport::Configure:
          return WasmInvocationResult::success(mobagen_wasm_plugin_configure_v1(arguments[0], arguments[1]));
        case mobagen::plugins::WasmPluginExport::Start:
          return WasmInvocationResult::success(mobagen_wasm_plugin_start_v1());
        case mobagen::plugins::WasmPluginExport::Quiesce:
          return WasmInvocationResult::success(mobagen_wasm_plugin_quiesce_v1());
        case mobagen::plugins::WasmPluginExport::Stop:
          return WasmInvocationResult::success(mobagen_wasm_plugin_stop_v1());
        case mobagen::plugins::WasmPluginExport::Process:
          ++calls_->process;
          return WasmInvocationResult::success(mobagen_wasm_plugin_process_v1(arguments[0], arguments[1], arguments[2]));
      }
      return WasmInvocationResult::failure("unknown portable asset-store export");
    }

    std::span<const std::byte> memory() const noexcept override {
      return {
          reinterpret_cast<const std::byte*>(mobagen_wasm_asset_store_test_memory_v1()),
          mobagen_wasm_asset_store_test_memory_size_v1(),
      };
    }

    std::span<std::byte> writable_memory() noexcept override {
      return {
          reinterpret_cast<std::byte*>(mobagen_wasm_asset_store_test_memory_v1()),
          mobagen_wasm_asset_store_test_memory_size_v1(),
      };
    }

  private:
    std::shared_ptr<ProviderCalls> calls_;
  };

  MobagenWasmAssetIdV1 asset_id(std::uint8_t seed) {
    MobagenWasmAssetIdV1 id{};
    for (std::size_t index = 0; index < std::size(id.bytes); ++index) {
      id.bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return id;
  }

}  // namespace

TEST_CASE("WASM asset store: portable provider processes acquired assets in batches") {
  using namespace mobagen::plugins;
  const std::array first_payload{std::byte{1}, std::byte{2}, std::byte{3}};
  const std::array second_payload{std::byte{8}, std::byte{13}};
  const auto first_id = asset_id(11);
  const auto second_id = asset_id(37);
  const std::array seeds{
      WasmAssetSeed{first_id, first_payload},
      WasmAssetSeed{second_id, second_payload},
  };
  auto configuration = build_wasm_asset_configuration(seeds);
  REQUIRE(configuration.ok());
  auto calls = std::make_shared<ProviderCalls>();

  auto activated = activate_portable_wasm_plugin(std::make_unique<PortableAssetStoreInstance>(calls), configuration.bytes);

  REQUIRE(activated.ok());
  CHECK(activated.activation->provider().id == "mobagen.assets.default");
  CHECK(activated.activation->provider().provides == std::vector<std::string>{MOBAGEN_WASM_ASSET_STORE_V1_ID});
  auto opened = activated.activation->open_command_channel(256, 512);
  REQUIRE(opened.ok());
  WasmAssetCommandBuffer commands{8, 256};
  REQUIRE(commands.queue_acquire(100, first_id));
  REQUIRE(commands.queue_acquire(101, second_id));

  auto acquired = commands.process(*opened.channel);

  REQUIRE(acquired.ok());
  CHECK(calls->process == 1);
  CHECK(acquired.responses->size() == 2);
  std::size_t cursor = 0;
  WasmAssetResponseView first_acquire;
  WasmAssetResponseView second_acquire;
  REQUIRE(acquired.responses->next(cursor, first_acquire));
  REQUIRE(acquired.responses->next(cursor, second_acquire));
  CHECK(cursor == acquired.responses->bytes().size());
  CHECK(first_acquire.request_id == 100);
  CHECK(first_acquire.status == MOBAGEN_WASM_STATUS_OK);
  CHECK(first_acquire.handle.generation != 0);
  CHECK(second_acquire.request_id == 101);
  CHECK(second_acquire.status == MOBAGEN_WASM_STATUS_OK);

  commands.clear();
  REQUIRE(commands.queue_view(200, first_acquire.handle));
  REQUIRE(commands.queue_view(201, second_acquire.handle));
  auto viewed = commands.process(*opened.channel);
  REQUIRE(viewed.ok());
  CHECK(calls->process == 2);
  cursor = 0;
  WasmAssetResponseView first_view;
  WasmAssetResponseView second_view;
  REQUIRE(viewed.responses->next(cursor, first_view));
  REQUIRE(viewed.responses->next(cursor, second_view));
  CHECK(std::ranges::equal(first_view.payload, first_payload));
  CHECK(std::ranges::equal(second_view.payload, second_payload));

  commands.clear();
  REQUIRE(commands.queue_release(300, first_acquire.handle));
  REQUIRE(commands.queue_release(301, second_acquire.handle));
  REQUIRE(commands.process(*opened.channel).ok());
  CHECK(calls->process == 3);

  commands.clear();
  REQUIRE(commands.queue_view(400, first_acquire.handle));
  auto stale = commands.process(*opened.channel);
  REQUIRE(stale.ok());
  cursor = 0;
  WasmAssetResponseView stale_view;
  REQUIRE(stale.responses->next(cursor, stale_view));
  CHECK(stale_view.status == MOBAGEN_WASM_STATUS_NOT_FOUND);
  CHECK(stale_view.payload.empty());
  REQUIRE(activated.activation->quiesce().ok());
  REQUIRE(activated.activation->stop().ok());
}

TEST_CASE("WASM asset store: warmed batched dispatch allocates nothing on the host") {
  using namespace mobagen::plugins;
  const std::array payload{std::byte{21}, std::byte{34}};
  const auto id = asset_id(72);
  const std::array seeds{WasmAssetSeed{id, payload}};
  auto configuration = build_wasm_asset_configuration(seeds);
  REQUIRE(configuration.ok());
  auto calls = std::make_shared<ProviderCalls>();
  auto activated = activate_portable_wasm_plugin(std::make_unique<PortableAssetStoreInstance>(calls), configuration.bytes);
  REQUIRE(activated.ok());
  auto opened = activated.activation->open_command_channel(128, 128);
  REQUIRE(opened.ok());
  WasmAssetCommandBuffer commands{2, 128};
  REQUIRE(commands.queue_acquire(1, id));
  auto acquired = commands.process(*opened.channel);
  REQUIRE(acquired.ok());
  std::size_t cursor = 0;
  WasmAssetResponseView response;
  REQUIRE(acquired.responses->next(cursor, response));

  commands.clear();
  REQUIRE(commands.queue_view(2, response.handle));
  REQUIRE(commands.process(*opened.channel).ok());
  module_allocation_probe::count.store(0, std::memory_order_relaxed);
  module_allocation_probe::enabled.store(true, std::memory_order_release);
  const auto measured = commands.process(*opened.channel);
  module_allocation_probe::enabled.store(false, std::memory_order_release);

  REQUIRE(measured.ok());
  CHECK(module_allocation_probe::count.load(std::memory_order_relaxed) == 0);
  CHECK(calls->process == 3);
  REQUIRE(activated.activation->quiesce().ok());
  REQUIRE(activated.activation->stop().ok());
}

TEST_CASE("WASM asset store: configuration and output limits fail closed") {
  using namespace mobagen::plugins;
  const std::array payload{std::byte{55}};
  const auto id = asset_id(91);

  SUBCASE("duplicate content identities are rejected by the provider") {
    const std::array seeds{
        WasmAssetSeed{id, payload},
        WasmAssetSeed{id, payload},
    };
    auto configuration = build_wasm_asset_configuration(seeds);
    REQUIRE(configuration.ok());
    auto activated
        = activate_portable_wasm_plugin(std::make_unique<PortableAssetStoreInstance>(std::make_shared<ProviderCalls>()), configuration.bytes);
    CHECK_FALSE(activated.ok());
    REQUIRE(activated.issues.size() == 1);
    CHECK(activated.issues.front().phase == WasmPluginExport::Configure);
    CHECK(activated.issues.front().status == MOBAGEN_WASM_STATUS_CONFLICT);
  }

  SUBCASE("response capacity is enforced inside the sandbox") {
    const std::array<std::byte, 33> large_payload{};
    const std::array seeds{WasmAssetSeed{id, large_payload}};
    auto configuration = build_wasm_asset_configuration(seeds);
    REQUIRE(configuration.ok());
    auto activated
        = activate_portable_wasm_plugin(std::make_unique<PortableAssetStoreInstance>(std::make_shared<ProviderCalls>()), configuration.bytes);
    REQUIRE(activated.ok());
    auto opened = activated.activation->open_command_channel(64, 64);
    REQUIRE(opened.ok());
    WasmAssetCommandBuffer commands{1, 64};
    REQUIRE(commands.queue_acquire(1, id));
    auto acquired = commands.process(*opened.channel);
    REQUIRE(acquired.ok());
    std::size_t cursor = 0;
    WasmAssetResponseView response;
    REQUIRE(acquired.responses->next(cursor, response));
    commands.clear();
    REQUIRE(commands.queue_view(2, response.handle));

    const auto viewed = commands.process(*opened.channel);

    CHECK_FALSE(viewed.ok());
    REQUIRE(viewed.issues.size() == 1);
    CHECK(viewed.issues[0].code == WasmAssetStoreIssueCode::ChannelFailure);
    REQUIRE(activated.activation->quiesce().ok());
    REQUIRE(activated.activation->stop().ok());
  }
}
