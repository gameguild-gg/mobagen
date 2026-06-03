#pragma once
// ============================================================================
// TransformSystem — computes world matrices for the Transform hierarchy.
// ============================================================================
// world = parent.world * local, resolved recursively with per-pass memoization
// (each entity computed once even if visited as several children's ancestor).
// Recompute-all per update for now; dirty-subtree skipping (needs child lists or
// a sorted order) is a later optimization. This is an ECS *system* — it can be
// parallelized over the scheduler the same way as systems-as-jobs.

#include "transform.hpp"
#include "world.hpp"

#include <cstdint>
#include <vector>

namespace scene {

class TransformSystem {
public:
    void update(ecs::World& w) {
        resolved_.assign(resolved_.size(), 0);
        w.view<Transform>([&](ecs::Entity e, Transform&) { resolve(w, e); });
    }

private:
    const glm::mat4& resolve(ecs::World& w, ecs::Entity e) {
        Transform& t = w.get<Transform>(e);
        const std::uint32_t idx = ecs::entity_index(e);
        if (idx < resolved_.size() && resolved_[idx]) return t.world;

        const glm::mat4 local = t.local();
        if (t.parent != ecs::kInvalidEntity && w.valid(t.parent) && w.has<Transform>(t.parent))
            t.world = resolve(w, t.parent) * local;   // parent first
        else
            t.world = local;

        if (idx >= resolved_.size()) resolved_.resize(idx + 1, 0);
        resolved_[idx] = 1;
        t.dirty = false;
        return t.world;
    }

    std::vector<std::uint8_t> resolved_;  // per-pass memo, by entity index
};

}  // namespace scene
