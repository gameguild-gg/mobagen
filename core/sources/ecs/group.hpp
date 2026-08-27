#pragma once
// ============================================================================
// Group<A,B> — co-ordered pools for contiguous multi-component iteration.
// ============================================================================
// The sparse-set weakness is view<A,B>: walk A packed, then a RANDOM sparse probe
// into B per entity. A Group fixes the locality by partitioning BOTH pools so the
// entities having A *and* B sit at positions [0,k) of both, in the SAME order.
// Then each() reads A and B by the same dense index — contiguous, zero probes
// (archetype-grade locality, while keeping sparse-set's cheap add/remove elsewhere).
//
// refresh() re-partitions (call after structural changes); each() iterates the
// co-ordered prefix. (A full owning group maintains this incrementally; this is
// the simpler refresh-on-demand form.)

#include "world.hpp"

#include <cstddef>
#include <cstdint>

namespace ecs {

  template <class A, class B> class Group {
  public:
    explicit Group(World& w) : w_(w) {}

    void refresh() {
      auto& sa = w_.storage<A>();
      auto& sb = w_.storage<B>();
      k_ = 0;
      const std::size_t n = sa.size();
      for (std::size_t p = 0; p < n; ++p) {  // standard partition over A
        const std::uint32_t e = sa.ids()[p];
        if (sb.contains(e)) {
          if (p != k_) sa.swap_dense(static_cast<std::uint32_t>(p), static_cast<std::uint32_t>(k_));
          const std::uint32_t bp = sb.index(e);
          if (bp != k_) sb.swap_dense(bp, static_cast<std::uint32_t>(k_));
          ++k_;
        }
      }
    }

    // Iterate the co-ordered prefix: fn(A&, B&). Both read by the same dense index
    // — contiguous in both pools, no sparse probe.
    template <class Fn> void each(Fn&& fn) {
      auto& sa = w_.storage<A>();
      auto& sb = w_.storage<B>();
      for (std::size_t i = 0; i < k_; ++i) fn(sa.data_at(i), sb.data_at(i));
    }

    std::size_t size() const { return k_; }

  private:
    World& w_;
    std::size_t k_ = 0;
  };

}  // namespace ecs
