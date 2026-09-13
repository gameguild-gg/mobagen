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

#include "binary_reader.hpp"
#include "render_bridge.hpp"  // render::VolumeRenderable (pulls ecs + scene)
#include "transform.hpp"
#include "world.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace render {

  inline constexpr std::uint32_t kSceneMagic = 0x4e435344u;  // 'D','S','C','N' (LE)
  inline constexpr std::uint32_t kSceneVersion = 2u;
  inline constexpr std::uint32_t kMaxSceneNodes = 100'000u;
  inline constexpr std::uint32_t kMaxVolumeDimension = 16'384u;

  namespace detail {
    template <class T> void put(std::vector<std::uint8_t>& b, const T& v) {
      static_assert(std::is_trivially_copyable_v<T>, "POD only");
      const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
      b.insert(b.end(), p, p + sizeof(T));
    }
    inline bool finite(const glm::vec3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

    inline bool finite(const glm::quat& value) {
      return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
    }

    inline bool valid(const scene::Transform& transform) {
      return finite(transform.position) && finite(transform.rotation) && finite(transform.scale);
    }

    inline bool valid(const VolumeRenderable& volume) {
      const VolumeSource& source = volume.source;
      const VolumeDisplay& display = volume.display;
      const bool valid_dimensions = source.width > 0 && source.height > 0 && source.depth > 0 && source.width <= kMaxVolumeDimension
                                    && source.height <= kMaxVolumeDimension && source.depth <= kMaxVolumeDimension;
      const bool valid_spacing = finite(source.spacing_mm) && source.spacing_mm.x > 0.0f && source.spacing_mm.y > 0.0f && source.spacing_mm.z > 0.0f;
      const bool valid_display = std::isfinite(display.window_center) && std::isfinite(display.window_width) && display.window_width > 0.0f
                                 && std::isfinite(display.iso_threshold) && display.transfer_preset >= 1 && display.transfer_preset <= 4;
      return valid_dimensions && valid_spacing && valid_display;
    }
  }  // namespace detail

  inline constexpr std::size_t kMinSerializedNodeBytes = sizeof(float) * 10 + sizeof(std::int32_t) + sizeof(std::uint8_t);

  // Serialize every entity that has a scene::Transform (the scene nodes), plus its
  // render::VolumeRenderable when present.
  inline std::vector<std::uint8_t> save_scene(ecs::World& world) {
    using detail::put;

    std::vector<ecs::Entity> order;
    world.view<scene::Transform>([&](ecs::Entity e, scene::Transform&) { order.push_back(e); });
    if (order.size() > kMaxSceneNodes) {
      throw std::length_error("scene exceeds the serialization node limit");
    }

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
        put(b, v.source.handle.index);
        put(b, v.source.handle.generation);
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
    if (data == nullptr) return {};
    serialization::BinaryReader reader(std::span<const std::byte>{reinterpret_cast<const std::byte*>(data), n});

    std::uint32_t magic = 0, version = 0, count = 0;
    if (!reader.read(magic) || magic != kSceneMagic) return {};
    if (!reader.read(version) || version != kSceneVersion) return {};
    if (!reader.read(count) || count > kMaxSceneNodes) return {};
    if (count > reader.remaining() / kMinSerializedNodeBytes) return {};

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
      if (!reader.read(t.position.x) || !reader.read(t.position.y) || !reader.read(t.position.z) || !reader.read(t.rotation.x)
          || !reader.read(t.rotation.y) || !reader.read(t.rotation.z) || !reader.read(t.rotation.w) || !reader.read(t.scale.x)
          || !reader.read(t.scale.y) || !reader.read(t.scale.z) || !reader.read(nd.parentIdx))
        return {};
      if (!detail::valid(t) || nd.parentIdx < -1 || nd.parentIdx >= static_cast<std::int32_t>(count)) return {};
      std::uint8_t hasVol = 0;
      if (!reader.read(hasVol) || hasVol > 1) return {};
      nd.hasVol = hasVol == 1;
      if (nd.hasVol) {
        VolumeRenderable& v = nd.vol;
        std::uint8_t fmt = 0, mode = 0;
        if (!reader.read(v.source.handle.index) || !reader.read(v.source.handle.generation) || !reader.read(v.source.width)
            || !reader.read(v.source.height) || !reader.read(v.source.depth) || !reader.read(v.source.spacing_mm.x)
            || !reader.read(v.source.spacing_mm.y) || !reader.read(v.source.spacing_mm.z) || !reader.read(fmt)
            || !reader.read(v.display.window_center) || !reader.read(v.display.window_width) || !reader.read(v.display.transfer_preset)
            || !reader.read(mode) || !reader.read(v.display.iso_threshold))
          return {};
        if (fmt > static_cast<std::uint8_t>(VolumeScalarFormat::Float32) || mode > static_cast<std::uint8_t>(VolumeRenderMode::Isosurface)) return {};
        v.source.format = static_cast<VolumeScalarFormat>(fmt);
        v.display.mode = static_cast<VolumeRenderMode>(mode);
        if (!detail::valid(v)) return {};
      }
    }
    if (reader.remaining() != 0) return {};

    // Create all entities first so parent indices can be remapped to handles.
    std::vector<ecs::Entity> created(count);
    for (std::uint32_t i = 0; i < count; ++i) created[i] = world.create();
    for (std::uint32_t i = 0; i < count; ++i) {
      Node& nd = nodes[i];
      nd.t.parent = nd.parentIdx >= 0 ? created[nd.parentIdx] : ecs::kInvalidEntity;
      nd.t.dirty = true;
      world.add<scene::Transform>(created[i], nd.t);
      if (nd.hasVol) world.add<VolumeRenderable>(created[i], nd.vol);
    }
    return created;
  }

}  // namespace render
