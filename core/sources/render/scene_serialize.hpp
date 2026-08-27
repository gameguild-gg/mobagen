#pragma once
// ============================================================================
// Scene serialization — save/load a DOD scene to a flat byte blob.
// ============================================================================
// The first persistence capability for the editor: an authored scene (the
// volume entity, its transform + display settings, a camera rig later) survives
// a save/load round trip. Pure C++/POD, no GPU.
//
// Stored per entity that has a scene::Transform: local TRS + parent, plus the
// render::VolumeRenderable if present. Derived state (Transform.world / dirty) is
// NOT stored — TransformSystem rebuilds it. Parents are written as indices into
// the saved list and remapped to fresh entity handles on load (entity ids are
// not stable across a load).

#include "render_bridge.hpp"  // render::VolumeRenderable (pulls ecs + scene)
#include "transform.hpp"
#include "world.hpp"

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace render {

  namespace detail {
    template <class T> void put(std::vector<std::uint8_t>& b, const T& v) {
      static_assert(std::is_trivially_copyable_v<T>, "POD only");
      const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
      b.insert(b.end(), p, p + sizeof(T));
    }
    template <class T> bool take(const std::uint8_t*& p, const std::uint8_t* end, T& out) {
      if (p + sizeof(T) > end) return false;
      std::memcpy(&out, p, sizeof(T));
      p += sizeof(T);
      return true;
    }
  }  // namespace detail

  inline constexpr std::uint32_t kSceneMagic = 0x4e435344u;  // 'D','S','C','N' (LE)
  inline constexpr std::uint32_t kSceneVersion = 1u;

  // Serialize every entity that has a scene::Transform (the scene nodes), plus its
  // render::VolumeRenderable when present.
  inline std::vector<std::uint8_t> save_scene(ecs::World& world) {
    using detail::put;

    std::vector<ecs::Entity> order;
    world.view<scene::Transform>([&](ecs::Entity e, scene::Transform&) { order.push_back(e); });

    auto save_index = [&](ecs::Entity e) -> std::int32_t {
      for (std::size_t i = 0; i < order.size(); ++i)
        if (order[i] == e) return static_cast<std::int32_t>(i);
      return -1;
    };

    std::vector<std::uint8_t> b;
    put(b, kSceneMagic);
    put(b, kSceneVersion);
    put(b, static_cast<std::uint32_t>(order.size()));

    for (ecs::Entity e : order) {
      const scene::Transform& t = world.get<scene::Transform>(e);
      put(b, t.position.x);
      put(b, t.position.y);
      put(b, t.position.z);
      put(b, t.rotation.x);
      put(b, t.rotation.y);
      put(b, t.rotation.z);
      put(b, t.rotation.w);
      put(b, t.scale.x);
      put(b, t.scale.y);
      put(b, t.scale.z);
      put(b, save_index(t.parent));

      const bool hasVol = world.has<VolumeRenderable>(e);
      put(b, static_cast<std::uint8_t>(hasVol ? 1 : 0));
      if (hasVol) {
        const VolumeRenderable& v = world.get<VolumeRenderable>(e);
        put(b, v.source.id);
        put(b, v.source.width);
        put(b, v.source.height);
        put(b, v.source.depth);
        put(b, v.source.spacing_mm.x);
        put(b, v.source.spacing_mm.y);
        put(b, v.source.spacing_mm.z);
        put(b, static_cast<std::uint8_t>(v.source.format));
        put(b, v.display.window_center);
        put(b, v.display.window_width);
        put(b, v.display.transfer_preset);
        put(b, static_cast<std::uint8_t>(v.display.mode));
        put(b, v.display.iso_threshold);
      }
    }
    return b;
  }

  // Recreate the scene into `world`. Returns the created entities by save index, or
  // an empty vector on a parse error.
  inline std::vector<ecs::Entity> load_scene(ecs::World& world, const std::uint8_t* data, std::size_t n) {
    using detail::take;
    const std::uint8_t* p = data;
    const std::uint8_t* const end = data + n;

    std::uint32_t magic = 0, version = 0, count = 0;
    if (!take(p, end, magic) || magic != kSceneMagic) return {};
    if (!take(p, end, version) || version != kSceneVersion) return {};
    if (!take(p, end, count)) return {};

    struct Node {
      scene::Transform t;
      std::int32_t parentIdx = -1;
      bool hasVol = false;
      VolumeRenderable vol;
    };
    std::vector<Node> nodes(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      Node& nd = nodes[i];
      scene::Transform& t = nd.t;
      if (!take(p, end, t.position.x) || !take(p, end, t.position.y) || !take(p, end, t.position.z)) return {};
      if (!take(p, end, t.rotation.x) || !take(p, end, t.rotation.y) || !take(p, end, t.rotation.z) || !take(p, end, t.rotation.w)) return {};
      if (!take(p, end, t.scale.x) || !take(p, end, t.scale.y) || !take(p, end, t.scale.z)) return {};
      if (!take(p, end, nd.parentIdx)) return {};
      std::uint8_t hasVol = 0;
      if (!take(p, end, hasVol)) return {};
      nd.hasVol = (hasVol != 0);
      if (nd.hasVol) {
        VolumeRenderable& v = nd.vol;
        std::uint8_t fmt = 0, mode = 0;
        if (!take(p, end, v.source.id) || !take(p, end, v.source.width) || !take(p, end, v.source.height) || !take(p, end, v.source.depth)
            || !take(p, end, v.source.spacing_mm.x) || !take(p, end, v.source.spacing_mm.y) || !take(p, end, v.source.spacing_mm.z)
            || !take(p, end, fmt) || !take(p, end, v.display.window_center) || !take(p, end, v.display.window_width)
            || !take(p, end, v.display.transfer_preset) || !take(p, end, mode) || !take(p, end, v.display.iso_threshold))
          return {};
        v.source.format = static_cast<VolumeScalarFormat>(fmt);
        v.display.mode = static_cast<VolumeRenderMode>(mode);
      }
    }

    // Create all entities first so parent indices can be remapped to handles.
    std::vector<ecs::Entity> created(count);
    for (std::uint32_t i = 0; i < count; ++i) created[i] = world.create();
    for (std::uint32_t i = 0; i < count; ++i) {
      Node& nd = nodes[i];
      nd.t.parent = (nd.parentIdx >= 0 && nd.parentIdx < static_cast<std::int32_t>(count)) ? created[nd.parentIdx] : ecs::kInvalidEntity;
      nd.t.dirty = true;
      world.add<scene::Transform>(created[i], nd.t);
      if (nd.hasVol) world.add<VolumeRenderable>(created[i], nd.vol);
    }
    return created;
  }

}  // namespace render
