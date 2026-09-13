#pragma once
// ============================================================================
// TransformSystem — LINEARIZED hierarchy resolve.
// ============================================================================
// rebuild(): compute a flat parents-before-children order ONCE (when the
//   hierarchy STRUCTURE changes), plus each node's parent position in that order.
// update(): a tight linear sweep — world[i] = world[parent_pos[i]] * local. Since
//   parents come first, the parent's world is already done and read from a
//   CONTIGUOUS array (no recursion, no sparse parent-probe, no memo map).
// (Each node's own component is still a sparse fetch; the SoA/SIMD step removes
//  that next.) Call rebuild on add/remove/reparent; update every frame.

#include "transform.hpp"
#include "world.hpp"

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace scene {

  class TransformSystem {
  public:
    bool rebuild(ecs::World& w) {
      std::vector<ecs::Entity> ents;
      w.view<Transform>([&](ecs::Entity e, Transform&) { ents.push_back(e); });
      if (ents.size() >= kNoParent) return false;

      std::unordered_map<ecs::Entity, std::uint32_t> source_positions;
      source_positions.reserve(ents.size());
      for (std::uint32_t i = 0; i < ents.size(); ++i) source_positions.emplace(ents[i], i);

      std::vector<std::uint32_t> source_parents(ents.size(), kNoParent);
      for (std::uint32_t i = 0; i < ents.size(); ++i) {
        const Transform* transform = w.try_get<Transform>(ents[i]);
        if (transform == nullptr) return false;
        if (transform->parent == ecs::kInvalidEntity) continue;
        if (transform->parent == ents[i] || !w.valid(transform->parent)) return false;
        const auto parent = source_positions.find(transform->parent);
        if (parent == source_positions.end()) return false;
        source_parents[i] = parent->second;
      }

      // Iterative tri-color walk. Each node has at most one parent, so unwinding
      // each path directly produces a deterministic parents-before-children order.
      std::vector<std::uint8_t> state(ents.size(), 0);  // 0=unvisited, 1=visiting, 2=done
      std::vector<std::uint32_t> sorted_sources;
      std::vector<std::uint32_t> path;
      sorted_sources.reserve(ents.size());
      path.reserve(ents.size());
      for (std::uint32_t start = 0; start < ents.size(); ++start) {
        if (state[start] == 2) continue;
        path.clear();
        std::uint32_t current = start;
        while (state[current] != 2) {
          if (state[current] == 1) return false;
          state[current] = 1;
          path.push_back(current);
          const std::uint32_t parent = source_parents[current];
          if (parent == kNoParent) break;
          current = parent;
        }
        while (!path.empty()) {
          const std::uint32_t source = path.back();
          path.pop_back();
          state[source] = 2;
          sorted_sources.push_back(source);
        }
      }

      std::vector<std::uint32_t> sorted_positions(ents.size());
      for (std::uint32_t i = 0; i < sorted_sources.size(); ++i) {
        sorted_positions[sorted_sources[i]] = i;
      }

      std::vector<ecs::Entity> next_order;
      std::vector<std::uint32_t> next_parent_positions;
      next_order.reserve(ents.size());
      next_parent_positions.reserve(ents.size());
      for (const std::uint32_t source : sorted_sources) {
        next_order.push_back(ents[source]);
        const std::uint32_t parent = source_parents[source];
        next_parent_positions.push_back(parent == kNoParent ? kNoParent
                                                            : sorted_positions[parent]);
      }
      std::vector<glm::mat4> next_world(ents.size(), glm::mat4(1.0f));
      std::vector<std::uint8_t> next_active(ents.size(), 0);

      order_ = std::move(next_order);
      parent_pos_ = std::move(next_parent_positions);
      world_ = std::move(next_world);
      active_ = std::move(next_active);
      return true;
    }

    void update(ecs::World& w) {
      for (std::size_t i = 0; i < order_.size(); ++i) {
        Transform* transform = w.try_get<Transform>(order_[i]);
        if (transform == nullptr) {
          active_[i] = 0;
          continue;
        }
        const glm::mat4 local = transform->local();
        const std::uint32_t parent = parent_pos_[i];
        world_[i] = parent != kNoParent && active_[parent] != 0 ? world_[parent] * local
                                                               : local;
        active_[i] = 1;
        transform->world = world_[i];
      }
    }

    std::size_t size() const { return order_.size(); }

  private:
    static constexpr std::uint32_t kNoParent = std::numeric_limits<std::uint32_t>::max();

    std::vector<ecs::Entity> order_;  // parents-before-children
    std::vector<std::uint32_t> parent_pos_;  // position in order_ (kNoParent = root)
    std::vector<glm::mat4> world_;           // computed world matrices, parallel to order_
    std::vector<std::uint8_t> active_;       // current update reached a live transform
  };

}  // namespace scene
