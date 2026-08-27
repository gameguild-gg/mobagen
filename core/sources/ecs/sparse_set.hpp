#pragma once
// ============================================================================
// SparseSet — the core ECS data structure (the EnTT model, our implementation).
// ============================================================================
// Maps integer ids -> a PACKED "dense" array (no gaps), giving:
//   - O(1) contains / index / insert / erase,
//   - cache-friendly linear iteration over the dense array.
//
// The "sparse" side (id -> dense position) is PAGED: allocated in fixed pages on
// demand, so a few high/scattered ids don't force one huge array. Erase uses
// swap-with-last + pop, which keeps the dense array packed in O(1).
//
// Storage<T> (storage.hpp) layers component values on top, mirroring every swap.

#include <cstdint>
#include <memory>
#include <vector>

namespace ecs {

  class SparseSet {
  public:
    virtual ~SparseSet() = default;  // World holds base pointers to Storage<T>

    static constexpr std::uint32_t npos = 0xFFFFFFFFu;  // "absent"

    bool contains(std::uint32_t id) const {
      const std::uint32_t p = id / page_size;
      return p < pages_.size() && pages_[p] && pages_[p][id % page_size] != npos;
    }

    // Dense position of id. Precondition: contains(id).
    std::uint32_t index(std::uint32_t id) const { return pages_[id / page_size][id % page_size]; }

    // Append id; returns its dense index. Precondition: !contains(id).
    std::uint32_t insert(std::uint32_t id) {
      const std::uint32_t at = static_cast<std::uint32_t>(dense_.size());
      sparse_ref(id) = at;
      dense_.push_back(id);
      return at;
    }

    // Remove id via swap-with-last + pop (keeps dense packed). Returns the dense
    // slot that was backfilled, so a parallel component store mirrors the swap.
    std::uint32_t erase(std::uint32_t id) {
      const std::uint32_t at = index(id);
      const std::uint32_t last = static_cast<std::uint32_t>(dense_.size() - 1);
      const std::uint32_t last_id = dense_[last];
      dense_[at] = last_id;                           // last id fills the hole
      sparse_ref(last_id) = at;                       // and now points there
      pages_[id / page_size][id % page_size] = npos;  // id becomes absent
      dense_.pop_back();
      return at;
    }

    // Type-erased per-entity removal (World::destroy). Base = id-only swap; a typed
    // Storage<T> overrides to also drop the component value.
    virtual void remove(std::uint32_t id) { (void)erase(id); }

    std::size_t size() const { return dense_.size(); }
    const std::uint32_t* ids() const { return dense_.data(); }  // packed id array

    // Swap the two dense positions i and j (and fix sparse). Used by Group to
    // co-order pools. Both positions must hold present ids.
    void swap_ids(std::uint32_t i, std::uint32_t j) {
      const std::uint32_t a = dense_[i], b = dense_[j];
      sparse_ref(a) = j;
      sparse_ref(b) = i;
      dense_[i] = b;
      dense_[j] = a;
    }

  protected:
    static constexpr std::uint32_t page_size = 1024;

    std::uint32_t& sparse_ref(std::uint32_t id) {
      const std::uint32_t p = id / page_size;
      if (p >= pages_.size()) pages_.resize(p + 1);
      if (!pages_[p]) {
        pages_[p] = std::make_unique<std::uint32_t[]>(page_size);
        for (std::uint32_t i = 0; i < page_size; ++i) pages_[p][i] = npos;
      }
      return pages_[p][id % page_size];
    }

    std::vector<std::unique_ptr<std::uint32_t[]>> pages_;  // sparse: id -> dense idx
    std::vector<std::uint32_t> dense_;                     // packed ids
  };

}  // namespace ecs
