#pragma once
// ============================================================================
// ResourceRegistry<T> — stable-handle asset store (DOD).
// ============================================================================
// Big assets (CPU volume buffers now; meshes/textures/GPU objects later) are
// OWNED here, in a packed array. Consumers hold a small Handle {index,
// generation}, never a raw pointer — so the render bridge's VolumeSource.id
// becomes a real handle into a registry instead of a bare integer.
//
// Releasing a slot bumps its generation, so any stale handle resolves to null
// (use-after-free safety), mirroring the ECS entity index+generation scheme.
// Freed slots are recycled via a free list.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace resource {

  template <class T> class ResourceRegistry {
  public:
    struct Handle {
      std::uint32_t index = 0;
      std::uint32_t generation = 0xFFFFFFFFu;  // distinct from any live slot gen
      bool operator==(const Handle& o) const { return index == o.index && generation == o.generation; }
      bool operator!=(const Handle& o) const { return !(*this == o); }
    };

    static constexpr Handle null_handle() { return Handle{0, 0xFFFFFFFFu}; }

    template <class... Args> Handle create(Args&&... args) {
      std::uint32_t idx;
      if (!free_.empty()) {
        idx = free_.back();
        free_.pop_back();
        slots_[idx].value = T(std::forward<Args>(args)...);
        slots_[idx].alive = true;  // generation kept from release()
      } else {
        idx = static_cast<std::uint32_t>(slots_.size());
        slots_.push_back(Slot{T(std::forward<Args>(args)...), 0u, true});
      }
      return Handle{idx, slots_[idx].generation};
    }

    bool valid(Handle h) const { return h.index < slots_.size() && slots_[h.index].alive && slots_[h.index].generation == h.generation; }

    T* get(Handle h) { return valid(h) ? &slots_[h.index].value : nullptr; }
    const T* get(Handle h) const { return valid(h) ? &slots_[h.index].value : nullptr; }

    void release(Handle h) {
      if (!valid(h)) return;
      slots_[h.index].alive = false;
      ++slots_[h.index].generation;  // invalidate outstanding handles to this slot
      slots_[h.index].value = T{};   // drop the payload (frees CPU/GPU bytes)
      free_.push_back(h.index);
    }

    std::size_t size() const { return slots_.size() - free_.size(); }
    std::size_t capacity() const { return slots_.size(); }

  private:
    struct Slot {
      T value;
      std::uint32_t generation = 0;
      bool alive = false;
    };
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
  };

}  // namespace resource
