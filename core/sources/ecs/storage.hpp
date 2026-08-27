#pragma once
// ============================================================================
// Storage<T> — SparseSet + component values, in a CHUNKED ARENA.
// ============================================================================
// Component values are kept packed (parallel to the SparseSet's dense ids) but
// split into fixed-size CHUNKS. Growing allocates a *new* chunk, so existing
// elements NEVER move:
//   - stable pointers/references to components,
//   - no realloc + copy churn (unlike a flat std::vector),
//   - controlled, predictable memory layout (one cache-friendly run per chunk).
//
// This is the memory-management + data-layout answer to the sparse-set critique.
// Big data (voxels, masks) still does NOT live here — the ECS holds handles; a
// `VolumeRef` component points at a GPU texture / arena buffer (ENGINE_ARCHITECTURE §5).

#include "sparse_set.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace ecs {

  // ChunkElems defaults to ~16 KB worth of T (a sensible cache/paging granule).
  template <class T, std::size_t ChunkElems = (sizeof(T) >= 16384 ? 1 : 16384 / sizeof(T))> class Storage : public SparseSet {
  public:
    static constexpr std::size_t chunk_elems = ChunkElems;

    template <class... Args> T& emplace(std::uint32_t id, Args&&... args) {
      const std::uint32_t at = insert(id);  // dense index of the new slot
      const std::size_t c = at / ChunkElems;
      if (c == chunks_.size()) {  // crossed into a new chunk
        chunks_.emplace_back();
        chunks_.back().reserve(ChunkElems);  // reserve => no realloc within a chunk
      }
      chunks_[c].emplace_back(std::forward<Args>(args)...);
      return chunks_[c].back();
    }

    void remove(std::uint32_t id) override {
      const std::uint32_t at = index(id);
      const std::uint32_t last = static_cast<std::uint32_t>(size() - 1);
      if (at != last) at_ref(at) = std::move(at_ref(last));  // mirror swap-with-last
      const std::size_t lc = last / ChunkElems;
      chunks_[lc].pop_back();  // drop the (moved-from) last
      if (chunks_[lc].empty() && lc != 0) chunks_.pop_back();
      erase(id);  // mirror the swap in the SparseSet
    }

    T& get(std::uint32_t id) { return at_ref(index(id)); }

    // Component at a dense position (lets a view be split into parallel ranges).
    T& data_at(std::size_t dense_index) { return at_ref(dense_index); }

    // Swap entity + component at two dense positions (Group co-ordering).
    void swap_dense(std::uint32_t i, std::uint32_t j) {
      std::swap(at_ref(i), at_ref(j));
      swap_ids(i, j);  // SparseSet
    }

    // Packed iteration: fn(id, T&). Walks each chunk's contiguous buffer through a
    // raw T* (one deref, no per-element div/mod) — the hot data-plane loop.
    template <class Fn> void each(Fn&& fn) {
      const std::uint32_t* id = ids();
      std::size_t i = 0;
      for (auto& chunk : chunks_) {
        T* base = chunk.data();
        const std::size_t cnt = chunk.size();
        for (std::size_t j = 0; j < cnt; ++j, ++i) fn(id[i], base[j]);
      }
    }

    // Chunk-aware iteration over a dense [begin, end) sub-range (for parallel_for):
    // div/mod only at chunk boundaries, then a raw-pointer stride within the chunk.
    template <class Fn> void each_range(std::size_t begin, std::size_t end, Fn&& fn) {
      const std::uint32_t* id = ids();
      std::size_t i = begin;
      while (i < end) {
        const std::size_t c = i / ChunkElems;
        T* base = chunks_[c].data();
        const std::size_t next = (c + 1) * ChunkElems;
        const std::size_t stop = end < next ? end : next;
        for (std::size_t o = i % ChunkElems; i < stop; ++i, ++o) fn(id[i], base[o]);
      }
    }

    std::size_t chunk_count() const { return chunks_.size(); }

  private:
    T& at_ref(std::size_t idx) { return chunks_[idx / ChunkElems][idx % ChunkElems]; }

    std::vector<std::vector<T>> chunks_;  // the arena: a list of fixed-capacity chunks
  };

}  // namespace ecs
