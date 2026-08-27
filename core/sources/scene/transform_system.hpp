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

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace scene {

  class TransformSystem {
  public:
    void rebuild(ecs::World& w) {
      std::vector<ecs::Entity> ents;
      w.view<Transform>([&](ecs::Entity e, Transform&) { ents.push_back(e); });

      std::unordered_map<std::uint32_t, int> depth;
      depth.reserve(ents.size());
      for (ecs::Entity e : ents) depth_of(w, e, depth);

      // parents (lower depth) sort before children
      std::stable_sort(ents.begin(), ents.end(),
                       [&](ecs::Entity a, ecs::Entity b) { return depth[ecs::entity_index(a)] < depth[ecs::entity_index(b)]; });

      std::unordered_map<std::uint32_t, int> pos;
      pos.reserve(ents.size());
      for (int i = 0; i < static_cast<int>(ents.size()); ++i) pos[ecs::entity_index(ents[i])] = i;

      order_ = std::move(ents);
      parent_pos_.assign(order_.size(), -1);
      for (int i = 0; i < static_cast<int>(order_.size()); ++i) {
        const Transform& t = w.get<Transform>(order_[i]);
        if (t.parent != ecs::kInvalidEntity) {
          auto it = pos.find(ecs::entity_index(t.parent));
          if (it != pos.end()) parent_pos_[i] = it->second;
        }
      }
      world_.assign(order_.size(), glm::mat4(1.0f));
    }

    void update(ecs::World& w) {
      for (std::size_t i = 0; i < order_.size(); ++i) {
        Transform& t = w.get<Transform>(order_[i]);
        const glm::mat4 local = t.local();
        world_[i] = parent_pos_[i] >= 0 ? world_[parent_pos_[i]] * local : local;
        t.world = world_[i];
      }
    }

    std::size_t size() const { return order_.size(); }

  private:
    int depth_of(ecs::World& w, ecs::Entity e, std::unordered_map<std::uint32_t, int>& memo) {
      const std::uint32_t idx = ecs::entity_index(e);
      auto it = memo.find(idx);
      if (it != memo.end()) return it->second;
      const Transform& t = w.get<Transform>(e);
      int d = 0;
      if (t.parent != ecs::kInvalidEntity && w.valid(t.parent) && w.has<Transform>(t.parent)) d = 1 + depth_of(w, t.parent, memo);
      memo[idx] = d;
      return d;
    }

    std::vector<ecs::Entity> order_;  // parents-before-children
    std::vector<int> parent_pos_;     // index of each node's parent in order_ (-1 = root)
    std::vector<glm::mat4> world_;    // computed world matrices, parallel to order_
  };

}  // namespace scene
