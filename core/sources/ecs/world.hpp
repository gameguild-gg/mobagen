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
#include "threading/thread_bound.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
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
      static std::atomic_size_t next{0};
      return next.fetch_add(1, std::memory_order_relaxed);
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
      thread_bound_.require_owner_thread();
      std::uint32_t idx;
      if (!free_.empty()) {
        idx = free_.back();
        free_.pop_back();
        alive_[idx] = 1;
      } else {
        idx = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(0);
        alive_.push_back(1);
      }
      return make_entity(idx, generations_[idx]);
    }

    bool valid(Entity e) const {
      const std::uint32_t i = entity_index(e);
      return i < generations_.size() && alive_[i] != 0 && generations_[i] == entity_gen(e);
    }

    bool destroy(Entity e) {
      thread_bound_.require_owner_thread();
      if (!valid(e)) return false;
      const std::uint32_t i = entity_index(e);
      for (auto& p : pools_)
        if (p && p->contains(i)) p->remove(i);  // type-erased
      ++generations_[i];                        // invalidate outstanding handles to this index
      alive_[i] = 0;
      free_.push_back(i);
      return true;
    }

    template <class T, class... Args> T& add(Entity e, Args&&... args) {
      thread_bound_.require_owner_thread();
      if (!valid(e)) throw std::invalid_argument("cannot add a component to an invalid entity");
      Storage<T>& components = storage<T>();
      const std::uint32_t index = entity_index(e);
      if (components.contains(index)) {
        throw std::logic_error("cannot add a duplicate component");
      }
      return components.emplace(index, std::forward<Args>(args)...);
    }

    template <class T> bool has(Entity e) const {
      if (!valid(e)) return false;
      const Storage<T>* components = find_storage<T>();
      return components != nullptr && components->contains(entity_index(e));
    }

    template <class T> T* try_get(Entity e) {
      if (!valid(e)) return nullptr;
      Storage<T>* components = find_storage<T>();
      return components != nullptr && components->contains(entity_index(e))
                 ? &components->get(entity_index(e))
                 : nullptr;
    }

    template <class T> const T* try_get(Entity e) const {
      if (!valid(e)) return nullptr;
      const Storage<T>* components = find_storage<T>();
      return components != nullptr && components->contains(entity_index(e))
                 ? &components->get(entity_index(e))
                 : nullptr;
    }

    template <class T> T& get(Entity e) {
      T* component = try_get<T>(e);
      if (component == nullptr) throw std::out_of_range("entity does not have the requested component");
      return *component;
    }

    template <class T> const T& get(Entity e) const {
      const T* component = try_get<T>(e);
      if (component == nullptr) throw std::out_of_range("entity does not have the requested component");
      return *component;
    }

    template <class T> bool remove(Entity e) {
      thread_bound_.require_owner_thread();
      if (!valid(e)) return false;
      Storage<T>* components = find_storage<T>();
      if (components == nullptr || !components->contains(entity_index(e))) return false;
      components->remove(entity_index(e));
      return true;
    }

    std::size_t alive() const { return generations_.size() - free_.size(); }

    // Single-component view: fn(Entity, T&). Packed iteration over T's pool.
    template <class T, class Fn> void view(Fn&& fn) {
      Storage<T>* components = find_storage<T>();
      if (components == nullptr) return;
      components->each(
          [&](std::uint32_t id, T& component) { fn(make_entity(id, generations_[id]), component); });
    }

    // Two-component view: iterate A's pool, gate on B. fn(Entity, A&, B&).
    template <class A, class B, class Fn> void view(Fn&& fn) {
      Storage<A>* first = find_storage<A>();
      Storage<B>* second = find_storage<B>();
      if (first == nullptr || second == nullptr) return;
      first->each([&](std::uint32_t id, A& a) {
        if (second->contains(id)) fn(make_entity(id, generations_[id]), a, second->get(id));
      });
    }

    // Number of entities with component A (size of A's packed pool).
    template <class A> std::size_t count() const {
      const Storage<A>* components = find_storage<A>();
      return components != nullptr ? components->size() : 0;
    }

    // Process A's dense range [begin, end), calling fn(Entity, A&, B&) for entities
    // that also have B. Split [0, count<A>()) into ranges and run them as jobs to get
    // "systems as jobs" — safe to call concurrently on DISJOINT ranges as long as no
    // components are added/removed during the pass.
    template <class A, class B, class Fn> void apply_range(std::size_t begin, std::size_t end, Fn&& fn) {
      Storage<A>* first = find_storage<A>();
      const std::size_t size = first != nullptr ? first->size() : 0;
      if (begin > end || end > size) throw std::out_of_range("component range is out of bounds");
      Storage<B>* second = find_storage<B>();
      if (first == nullptr || second == nullptr) return;
      first->each_range(begin, end, [&](std::uint32_t id, A& a) {  // A-side chunk-aware
        if (second->contains(id)) fn(make_entity(id, generations_[id]), a, second->get(id));
      });
    }

  private:
    template <class T> Storage<T>& storage() {
      const std::size_t id = detail::component_id<T>();
      if (id >= pools_.size()) pools_.resize(id + 1);
      if (!pools_[id]) pools_[id] = std::make_unique<Storage<T>>();
      return static_cast<Storage<T>&>(*pools_[id]);
    }

    template <class T> Storage<T>* find_storage() {
      const std::size_t id = detail::component_id<T>();
      return id < pools_.size() && pools_[id] != nullptr
                 ? static_cast<Storage<T>*>(pools_[id].get())
                 : nullptr;
    }

    template <class T> const Storage<T>* find_storage() const {
      const std::size_t id = detail::component_id<T>();
      return id < pools_.size() && pools_[id] != nullptr
                 ? static_cast<const Storage<T>*>(pools_[id].get())
                 : nullptr;
    }

    // Structural mutation is owner-only. Workers may access pre-existing
    // components through disjoint apply_range intervals while structure is frozen.
    threading::ThreadBound thread_bound_;
    std::vector<std::uint32_t> generations_;         // current generation per index
    std::vector<std::uint8_t> alive_;                 // slot occupancy, independent of generation
    std::vector<std::uint32_t> free_;                // recycled indices
    std::vector<std::unique_ptr<SparseSet>> pools_;  // component storages, by component id
  };

}  // namespace ecs
