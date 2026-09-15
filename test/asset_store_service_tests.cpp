#include <doctest/doctest.h>

#include "assets/asset_store_service.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace module_allocation_probe {
  extern std::atomic_bool enabled;
  extern std::atomic_size_t count;
}  // namespace module_allocation_probe

namespace {

  std::span<const std::byte> bytes(std::string_view value) { return {reinterpret_cast<const std::byte*>(value.data()), value.size()}; }

  class TemporaryAssetStoreDirectory {
  public:
    TemporaryAssetStoreDirectory() {
      static std::atomic_uint64_t sequence = 0;
      const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
      path_ = std::filesystem::temp_directory_path() / ("mobagen-asset-store-" + std::to_string(ticks) + '-' + std::to_string(sequence.fetch_add(1)));
      REQUIRE(std::filesystem::create_directory(path_));
    }

    ~TemporaryAssetStoreDirectory() {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
  };

  MobagenAssetIdV1 abi_id(const mobagen::assets::AssetId& id) {
    MobagenAssetIdV1 result{};
    std::ranges::copy(id.bytes, result.bytes);
    return result;
  }

}  // namespace

TEST_CASE("Asset store service: ABI acquires shared immutable blobs lazily") {
  using namespace mobagen::assets;
  TemporaryAssetStoreDirectory directory;
  AssetCache cache{directory.path()};
  const auto stored = cache.store(bytes("shared asset payload"));
  REQUIRE(stored.ok());
  REQUIRE(stored.id.has_value());
  NativeAssetStoreService service{cache};
  const auto& api = service.api();
  const auto id = abi_id(*stored.id);
  MobagenAssetHandleV1 first{};
  MobagenAssetHandleV1 second{};

  CHECK(api.header.struct_size == MOBAGEN_ASSET_STORE_V1_SIZE);
  CHECK(api.header.abi_version == MOBAGEN_ASSET_STORE_V1_ABI_VERSION);
  REQUIRE(api.store_state != nullptr);
  REQUIRE(api.acquire != nullptr);
  REQUIRE(api.view != nullptr);
  REQUIRE(api.release != nullptr);
  CHECK(api.acquire(nullptr, &id, &first) == MOBAGEN_STATUS_INVALID_ARGUMENT);
  CHECK(api.acquire(api.store_state, nullptr, &first) == MOBAGEN_STATUS_INVALID_ARGUMENT);
  CHECK(api.acquire(api.store_state, &id, nullptr) == MOBAGEN_STATUS_INVALID_ARGUMENT);

  const auto missing_internal = sha256(bytes("missing asset payload"));
  REQUIRE(missing_internal.has_value());
  const auto missing = abi_id(*missing_internal);
  CHECK(api.acquire(api.store_state, &missing, &first) == MOBAGEN_STATUS_NOT_FOUND);
  CHECK(api.view(api.store_state, first, nullptr) == MOBAGEN_STATUS_INVALID_ARGUMENT);
  CHECK(api.release(nullptr, first) == MOBAGEN_STATUS_INVALID_ARGUMENT);

  REQUIRE(api.acquire(api.store_state, &id, &first) == MOBAGEN_STATUS_OK);
  REQUIRE(std::filesystem::remove(stored.path));
  REQUIRE(api.acquire(api.store_state, &id, &second) == MOBAGEN_STATUS_OK);
  CHECK(first.index == second.index);
  CHECK(first.generation == second.generation);

  bool valid_views = true;
  module_allocation_probe::count.store(0, std::memory_order_relaxed);
  module_allocation_probe::enabled.store(true, std::memory_order_release);
  for (std::size_t index = 0; index < 1'024; ++index) {
    MobagenByteView view{};
    valid_views = valid_views && api.view(api.store_state, first, &view) == MOBAGEN_STATUS_OK && view.size == bytes("shared asset payload").size()
                  && std::equal(view.data, view.data + view.size, reinterpret_cast<const std::uint8_t*>("shared asset payload"));
  }
  module_allocation_probe::enabled.store(false, std::memory_order_release);
  CHECK(valid_views);
  CHECK(module_allocation_probe::count.load(std::memory_order_relaxed) == 0);

  CHECK(api.release(api.store_state, first) == MOBAGEN_STATUS_OK);
  MobagenByteView retained{};
  CHECK(api.view(api.store_state, second, &retained) == MOBAGEN_STATUS_OK);
  CHECK(api.release(api.store_state, second) == MOBAGEN_STATUS_OK);
  CHECK(api.view(api.store_state, second, &retained) == MOBAGEN_STATUS_NOT_FOUND);
  CHECK(api.release(api.store_state, second) == MOBAGEN_STATUS_NOT_FOUND);
}

TEST_CASE("Asset store service: ABI serializes concurrent leases") {
  using namespace mobagen::assets;
  TemporaryAssetStoreDirectory directory;
  AssetCache cache{directory.path()};
  const auto stored = cache.store(bytes("threaded asset"));
  REQUIRE(stored.ok());
  NativeAssetStoreService service{cache};
  const auto& api = service.api();
  const auto id = abi_id(*stored.id);
  MobagenAssetHandleV1 owner{};
  REQUIRE(api.acquire(api.store_state, &id, &owner) == MOBAGEN_STATUS_OK);

  std::atomic_bool succeeded = true;
  std::vector<std::thread> workers;
  for (std::size_t worker = 0; worker < 4; ++worker) {
    workers.emplace_back([&] {
      for (std::size_t iteration = 0; iteration < 256; ++iteration) {
        MobagenAssetHandleV1 handle{};
        MobagenByteView view{};
        if (api.acquire(api.store_state, &id, &handle) != MOBAGEN_STATUS_OK || api.view(api.store_state, handle, &view) != MOBAGEN_STATUS_OK
            || view.size != bytes("threaded asset").size() || api.release(api.store_state, handle) != MOBAGEN_STATUS_OK) {
          succeeded.store(false, std::memory_order_relaxed);
          return;
        }
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }

  CHECK(succeeded.load(std::memory_order_relaxed));
  CHECK(api.release(api.store_state, owner) == MOBAGEN_STATUS_OK);
  MobagenByteView stale{};
  CHECK(api.view(api.store_state, owner, &stale) == MOBAGEN_STATUS_NOT_FOUND);
}
