#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "assets/asset_dependency_graph.hpp"
#include "assets/asset_id.hpp"

namespace {

  std::span<const std::byte> bytes(std::string_view value) { return {reinterpret_cast<const std::byte*>(value.data()), value.size()}; }

  mobagen::assets::AssetId id_for(std::string_view value) {
    const auto id = mobagen::assets::sha256(bytes(value));
    REQUIRE(id.has_value());
    return *id;
  }

  std::size_t position_of(const std::vector<mobagen::assets::AssetId>& order, const mobagen::assets::AssetId& id) {
    const auto found = std::find(order.begin(), order.end(), id);
    REQUIRE(found != order.end());
    return static_cast<std::size_t>(std::distance(order.begin(), found));
  }

}  // namespace

TEST_CASE("Asset dependency graph: build order is deterministic and dependency first") {
  using mobagen::assets::AssetDependencyGraph;
  using mobagen::assets::AssetDependencyStatus;

  const auto source = id_for("source");
  const auto material = id_for("material");
  const auto mesh = id_for("mesh");
  const auto bundle = id_for("bundle");

  AssetDependencyGraph first;
  REQUIRE(first.register_asset(bundle));
  REQUIRE(first.register_asset(mesh));
  REQUIRE(first.register_asset(material));
  REQUIRE(first.register_asset(source));
  CHECK(first.set_dependencies(mesh, std::array{source}) == AssetDependencyStatus::success);
  CHECK(first.set_dependencies(material, std::array{source}) == AssetDependencyStatus::success);
  CHECK(first.set_dependencies(bundle, std::array{mesh, material}) == AssetDependencyStatus::success);

  AssetDependencyGraph second;
  REQUIRE(second.register_asset(source));
  REQUIRE(second.register_asset(material));
  REQUIRE(second.register_asset(mesh));
  REQUIRE(second.register_asset(bundle));
  CHECK(second.set_dependencies(bundle, std::array{material, mesh}) == AssetDependencyStatus::success);
  CHECK(second.set_dependencies(material, std::array{source}) == AssetDependencyStatus::success);
  CHECK(second.set_dependencies(mesh, std::array{source}) == AssetDependencyStatus::success);

  const auto first_order = first.build_order(std::array{bundle});
  const auto second_order = second.build_order(std::array{bundle});
  REQUIRE(first_order.status == AssetDependencyStatus::success);
  REQUIRE(second_order.status == AssetDependencyStatus::success);
  CHECK(first_order.assets == second_order.assets);
  CHECK(position_of(first_order.assets, source) < position_of(first_order.assets, material));
  CHECK(position_of(first_order.assets, source) < position_of(first_order.assets, mesh));
  CHECK(position_of(first_order.assets, material) < position_of(first_order.assets, bundle));
  CHECK(position_of(first_order.assets, mesh) < position_of(first_order.assets, bundle));
}

TEST_CASE("Asset dependency graph: invalid updates are rejected transactionally") {
  using mobagen::assets::AssetDependencyGraph;
  using mobagen::assets::AssetDependencyStatus;

  const auto first = id_for("first");
  const auto second = id_for("second");
  const auto missing = id_for("missing");
  AssetDependencyGraph graph;
  REQUIRE(graph.register_asset(first));
  REQUIRE(graph.register_asset(second));
  CHECK_FALSE(graph.register_asset(first));
  REQUIRE(graph.set_dependencies(first, std::array{second}) == AssetDependencyStatus::success);

  CHECK(graph.set_dependencies(missing, std::array<decltype(first), 0>{}) == AssetDependencyStatus::unknown_asset);
  CHECK(graph.set_dependencies(first, std::array{missing}) == AssetDependencyStatus::unknown_dependency);
  CHECK(graph.set_dependencies(first, std::array{first}) == AssetDependencyStatus::self_dependency);
  CHECK(graph.set_dependencies(first, std::array{second, second}) == AssetDependencyStatus::duplicate_dependency);

  const auto order = graph.build_order(std::array{first});
  REQUIRE(order.status == AssetDependencyStatus::success);
  REQUIRE(order.assets.size() == 2);
  CHECK(order.assets[0] == second);
  CHECK(order.assets[1] == first);
}

TEST_CASE("Asset dependency graph: cycles are rejected without changing the graph") {
  using mobagen::assets::AssetDependencyGraph;
  using mobagen::assets::AssetDependencyStatus;

  const auto first = id_for("cycle-first");
  const auto second = id_for("cycle-second");
  const auto third = id_for("cycle-third");
  AssetDependencyGraph graph;
  REQUIRE(graph.register_asset(first));
  REQUIRE(graph.register_asset(second));
  REQUIRE(graph.register_asset(third));
  REQUIRE(graph.set_dependencies(first, std::array{second}) == AssetDependencyStatus::success);
  REQUIRE(graph.set_dependencies(second, std::array{third}) == AssetDependencyStatus::success);

  CHECK(graph.set_dependencies(third, std::array{first}) == AssetDependencyStatus::cycle);
  const auto order = graph.build_order(std::array{first});
  REQUIRE(order.status == AssetDependencyStatus::success);
  REQUIRE(order.assets.size() == 3);
  CHECK(order.assets[0] == third);
  CHECK(order.assets[1] == second);
  CHECK(order.assets[2] == first);
}

TEST_CASE("Asset dependency graph: unknown roots fail without partial output") {
  using mobagen::assets::AssetDependencyGraph;
  using mobagen::assets::AssetDependencyStatus;

  AssetDependencyGraph graph;
  const auto missing = id_for("unknown-root");
  const auto order = graph.build_order(std::array{missing});
  CHECK(order.status == AssetDependencyStatus::unknown_asset);
  CHECK(order.assets.empty());
}
