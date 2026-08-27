#pragma once
// ============================================================================
// TransformStore — a dedicated Structure-of-Arrays transform store (data-oriented).
// ============================================================================
// Transform data lives in flat, parallel arrays (not in a generic ECS component),
// so update() is a PURE CONTIGUOUS SWEEP: no sparse probes, no entity indirection,
// perfect sequential locality, and SIMD-friendly matrix math. The ECS entity holds
// only a small TransformStore::Id (an index into these arrays).
//
// PRECONDITION for the fast sweep: parents are created before children, so a
// parent's index is < its child's index and world_[parent] is already computed
// when the child is reached (the linearized order, baked into creation order).

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace scene {

  class TransformStore {
  public:
    using Id = std::uint32_t;
    static constexpr Id npos = 0xFFFFFFFFu;

    Id create(Id parent = npos) {
      const Id id = static_cast<Id>(parent_.size());
      pos_.emplace_back(0.0f);
      rot_.emplace_back(1.0f, 0.0f, 0.0f, 0.0f);  // identity quat
      scl_.emplace_back(1.0f);
      parent_.push_back(parent == npos ? -1 : static_cast<int>(parent));
      world_.emplace_back(1.0f);
      return id;
    }

    void set_position(Id i, const glm::vec3& p) { pos_[i] = p; }
    void set_rotation(Id i, const glm::quat& q) { rot_[i] = q; }
    void set_scale(Id i, const glm::vec3& s) { scl_[i] = s; }
    const glm::mat4& world(Id i) const { return world_[i]; }
    std::size_t size() const { return parent_.size(); }

    // Pure SoA sweep over contiguous arrays — no probes. SIMD acts on the mat ops
    // when the TU is compiled with intrinsics (see CMake: AVX2 + GLM_FORCE_INTRINSICS).
    void update() {
      const std::size_t n = parent_.size();
      const glm::vec3* P = pos_.data();
      const glm::quat* R = rot_.data();
      const glm::vec3* S = scl_.data();
      const int* PA = parent_.data();
      glm::mat4* W = world_.data();
      for (std::size_t i = 0; i < n; ++i) {
        // Direct TRS compose: M = T * R * S built by hand — no wasted
        // near-identity 4x4 multiplies (just quat->3x3 + scaled columns + translation).
        const glm::mat3 rot = glm::mat3_cast(R[i]);
        glm::mat4 local;
        local[0] = glm::vec4(rot[0] * S[i].x, 0.0f);
        local[1] = glm::vec4(rot[1] * S[i].y, 0.0f);
        local[2] = glm::vec4(rot[2] * S[i].z, 0.0f);
        local[3] = glm::vec4(P[i], 1.0f);
        const int p = PA[i];
        W[i] = (p >= 0) ? W[p] * local : local;
      }
    }

  private:
    std::vector<glm::vec3> pos_;
    std::vector<glm::quat> rot_;
    std::vector<glm::vec3> scl_;
    std::vector<int> parent_;
    std::vector<glm::mat4> world_;
  };

}  // namespace scene
