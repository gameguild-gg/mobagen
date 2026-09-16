#include <doctest/doctest.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include "assets/asset_id.hpp"

namespace {

  std::span<const std::byte> bytes(std::string_view value) { return {reinterpret_cast<const std::byte*>(value.data()), value.size()}; }

}  // namespace

TEST_CASE("Asset ID: SHA-256 matches published vectors") {
  using mobagen::assets::sha256;
  using mobagen::assets::to_string;

  const auto empty = sha256({});
  REQUIRE(empty.has_value());
  CHECK(to_string(*empty) == "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  const auto abc = sha256(bytes("abc"));
  REQUIRE(abc.has_value());
  CHECK(to_string(*abc) == "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("Asset ID: streaming and one-shot hashing are identical") {
  mobagen::assets::Sha256Hasher streaming;
  REQUIRE(streaming.update(bytes("modu")));
  REQUIRE(streaming.update(bytes("lar-")));
  REQUIRE(streaming.update(bytes("asset")));
  const auto streamed = streaming.finish();
  const auto one_shot = mobagen::assets::sha256(bytes("modular-asset"));

  REQUIRE(streamed.has_value());
  REQUIRE(one_shot.has_value());
  CHECK(*streamed == *one_shot);
  CHECK_FALSE(streaming.update(bytes("late")));
  CHECK_FALSE(streaming.finish().has_value());
}

TEST_CASE("Asset ID: canonical text parsing is strict and deterministic") {
  using mobagen::assets::parse_asset_id;
  using mobagen::assets::to_string;

  constexpr std::string_view canonical = "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  const auto parsed = parse_asset_id(canonical);
  REQUIRE(parsed.has_value());
  CHECK(to_string(*parsed) == std::string{canonical});
  CHECK_FALSE(parse_asset_id("sha256:BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD").has_value());
  CHECK_FALSE(parse_asset_id("sha1:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad").has_value());
  CHECK_FALSE(parse_asset_id("sha256:short").has_value());
  CHECK_FALSE(parse_asset_id("sha256:gg7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad").has_value());
}

TEST_CASE("Asset ID: value type is ABI-friendly and hashable") {
  using mobagen::assets::AssetId;
  static_assert(std::is_standard_layout_v<AssetId>);
  static_assert(std::is_trivially_copyable_v<AssetId>);
  static_assert(sizeof(AssetId) == 32);

  const auto first = mobagen::assets::sha256(bytes("same"));
  const auto second = mobagen::assets::sha256(bytes("same"));
  const auto different = mobagen::assets::sha256(bytes("different"));
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  REQUIRE(different.has_value());
  CHECK(mobagen::assets::AssetIdHash{}(*first) == mobagen::assets::AssetIdHash{}(*second));
  CHECK(*first != *different);
}
