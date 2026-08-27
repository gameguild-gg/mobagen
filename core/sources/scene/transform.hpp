#pragma once
// ============================================================================
// Transform — the scene-graph node, as an ECS component (not a separate tree).
// ============================================================================
// Local TRS + a parent Entity + a cached world matrix. The hierarchy lives in
// the data (the `parent` field); TransformSystem turns it into world matrices.

#include "world.hpp"  // ecs::Entity

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace scene {

  struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identity (w, x, y, z)
    glm::vec3 scale{1.0f};
    ecs::Entity parent = ecs::kInvalidEntity;  // kInvalidEntity => root

    glm::mat4 world{1.0f};  // filled by TransformSystem
    bool dirty = true;

    glm::mat4 local() const { return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale); }
  };

}  // namespace scene
