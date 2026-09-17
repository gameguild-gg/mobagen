#include <doctest/doctest.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "assets/asset_id.hpp"
#include "assets/asset_registry.hpp"

namespace {

  std::span<const std::byte> bytes(std::string_view value) { return {reinterpret_cast<const std::byte*>(value.data()), value.size()}; }

  mobagen::assets::AssetId id_for(std::string_view value) {
    const auto id = mobagen::assets::sha256(bytes(value));
    REQUIRE(id.has_value());
    return *id;
  }

}  // namespace

TEST_CASE("Asset registry: content identity deduplicates payloads") {
  mobagen::assets::AssetRegistry<std::string> registry;
  const auto id = id_for("mesh bytes");

  const auto first = registry.emplace(id, "first payload");
  REQUIRE(first.inserted);
  CHECK(registry.size() == 1);
  REQUIRE(registry.get(first.handle) != nullptr);
  CHECK(*registry.get(first.handle) == "first payload");

  const auto duplicate = registry.emplace(id, "replacement payload");
  CHECK_FALSE(duplicate.inserted);
  CHECK(duplicate.handle == first.handle);
  CHECK(registry.size() == 1);
  CHECK(*registry.get(first.handle) == "first payload");

  const auto found = registry.find(id);
  REQUIRE(found.has_value());
  CHECK(*found == first.handle);
}

TEST_CASE("Asset registry: release invalidates stale handles and identity lookups") {
  mobagen::assets::AssetRegistry<std::string> registry;
  const auto id = id_for("texture bytes");
  const auto first = registry.emplace(id, "texture");
  REQUIRE(first.inserted);

  CHECK(registry.release(first.handle));
  CHECK_FALSE(registry.release(first.handle));
  CHECK(registry.get(first.handle) == nullptr);
  CHECK_FALSE(registry.find(id).has_value());
  CHECK(registry.size() == 0);

  const auto replacement = registry.emplace(id, "reloaded texture");
  REQUIRE(replacement.inserted);
  CHECK(replacement.handle.index == first.handle.index);
  CHECK(replacement.handle.generation != first.handle.generation);
  CHECK(registry.get(first.handle) == nullptr);
  REQUIRE(registry.get(replacement.handle) != nullptr);
  CHECK(*registry.get(replacement.handle) == "reloaded texture");
}

TEST_CASE("Asset registry: invalid handles are safe no-ops") {
  mobagen::assets::AssetRegistry<int> registry;

  CHECK(registry.get(resource::kNullHandle) == nullptr);
  CHECK_FALSE(registry.release(resource::kNullHandle));
  CHECK(registry.size() == 0);
  CHECK(registry.capacity() == 0);
}
