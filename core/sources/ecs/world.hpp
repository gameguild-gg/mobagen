#pragma once
// ============================================================================
// World — the ECS registry: entities + their component storages + views.
// ============================================================================
// Entity = 32-bit index + 32-bit generation packed in 64 bits. Destroying an
// entity bumps the index's generation, so stale handles fail valid() (catches
// use-after-destroy). Indices are recycled via a free list. Each component type
// gets its own Storage<T> (sparse-set + chunked arena), created on first use.

#include "sparse_set.hpp"
#include "storage.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace ecs {

  using Entity = std::uint64_t;
  constexpr Entity kInvalidEntity = ~Entity{0};

  inline std::uint32_t entity_index(Entity e) { return static_cast<std::uint32_t>(e & 0xFFFFFFFFu); }
  inline std::uint32_t entity_gen(Entity e) { return static_cast<std::uint32_t>(e >> 32); }
  inline Entity make_entity(std::uint32_t idx, std::uint32_t gen) { return (static_cast<Entity>(gen) << 32) | idx; }

  namespace detail {
    inline std::size_t next_component_id() {
      static std::size_t c = 0;
      return c++;
    }
    template <class T> std::size_t component_id() {
      static const std::size_t id = next_component_id();
      return id;
    }
  }  // namespace detail

  class World {
    template <class, class> friend class Group;  // Group co-orders our component pools

  public:
    Entity create() {
      std::uint32_t idx;
      if (!free_.empty()) {
        idx = free_.back();
        free_.pop_back();
      } else {
        idx = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(0);
      }
      return make_entity(idx, generations_[idx]);
    }

    bool valid(Entity e) const {
      const std::uint32_t i = entity_index(e);
      return i < generations_.size() && generations_[i] == entity_gen(e);
    }

    void destroy(Entity e) {
      if (!valid(e)) return;
      const std::uint32_t i = entity_index(e);
      for (auto& p : pools_)
        if (p && p->contains(i)) p->remove(i);  // type-erased
      ++generations_[i];                        // invalidate outstanding handles to this index
      free_.push_back(i);
    }

    template <class T, class... Args> T& add(Entity e, Args&&... args) { return storage<T>().emplace(entity_index(e), std::forward<Args>(args)...); }

    template <class T> bool has(Entity e) const {
      const std::size_t id = detail::component_id<T>();
      return id < pools_.size() && pools_[id] && pools_[id]->contains(entity_index(e));
    }

    template <class T> T& get(Entity e) { return storage<T>().get(entity_index(e)); }
    template <class T> void remove(Entity e) { storage<T>().remove(entity_index(e)); }

    std::size_t alive() const { return generations_.size() - free_.size(); }

    // Single-component view: fn(Entity, T&). Packed iteration over T's pool.
    template <class T, class Fn> void view(Fn&& fn) {
      storage<T>().each([&](std::uint32_t id, T& c) { fn(make_entity(id, generations_[id]), c); });
    }

    // Two-component view: iterate A's pool, gate on B. fn(Entity, A&, B&).
    template <class A, class B, class Fn> void view(Fn&& fn) {
      auto& sb = storage<B>();
      storage<A>().each([&](std::uint32_t id, A& a) {
        if (sb.contains(id)) fn(make_entity(id, generations_[id]), a, sb.get(id));
      });
    }

    // Number of entities with component A (size of A's packed pool).
    template <class A> std::size_t count() { return storage<A>().size(); }

    // Process A's dense range [begin, end), calling fn(Entity, A&, B&) for entities
    // that also have B. Split [0, count<A>()) into ranges and run them as jobs to get
    // "systems as jobs" — safe to call concurrently on DISJOINT ranges as long as no
    // components are added/removed during the pass.
    template <class A, class B, class Fn> void apply_range(std::size_t begin, std::size_t end, Fn&& fn) {
      auto& sb = storage<B>();
      storage<A>().each_range(begin, end, [&](std::uint32_t id, A& a) {  // A-side chunk-aware
        if (sb.contains(id)) fn(make_entity(id, generations_[id]), a, sb.get(id));
      });
    }

  private:
    template <class T> Storage<T>& storage() {
      const std::size_t id = detail::component_id<T>();
      if (id >= pools_.size()) pools_.resize(id + 1);
      if (!pools_[id]) pools_[id] = std::make_unique<Storage<T>>();
      return static_cast<Storage<T>&>(*pools_[id]);
    }

    std::vector<std::uint32_t> generations_;         // current generation per index
    std::vector<std::uint32_t> free_;                // recycled indices
    std::vector<std::unique_ptr<SparseSet>> pools_;  // component storages, by component id
  };

}  // namespace ecs
