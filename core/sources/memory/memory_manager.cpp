#include "memory_manager.hpp"

#include <algorithm>
#include <cstring>

namespace mobagen {
  namespace memory {

    namespace {

      /* Logical layout of the 64-byte control block. The epoch word and the
       * host-park word are shim-atomic; everything else is single-writer
       * (the manager) or quiesced-phase access.
       *
       *  +0  u32 magic ("MMG1") — init marker
       *  +4  u32 epoch  (bit0: 1 = GC in flight)
       *  +8  u32 host_park (notifies host threads parked at the barrier)
       *  +12 u32 live_objects
       *  +16 u32 in_flight (mutators inside the GC bracket; shim-atomic)
       *  +20 u64 total_allocs, +24 u64 total_frees (diagnostics; plain)
       */
      constexpr std::uint32_t control_magic_offset = 0;
      constexpr std::uint32_t control_epoch_offset = 4;
      constexpr std::uint32_t control_park_offset = 8;
      constexpr std::uint32_t control_live_offset = 12;
      constexpr std::uint32_t control_inflight_offset = 16;
      constexpr std::uint32_t control_magic = 0x4D4D4731U; /* "MMG1" */

      MemoryIssue make_issue(MemoryIssueCode code, std::string message) { return MemoryIssue{code, std::move(message)}; }

    }  // namespace

    MemoryManager::MemoryManager(Shim shim) : shim_(std::move(shim)) {}

    MemoryManager::~MemoryManager() {
      /* No region release in the vtable: region lifetime is owned by the
       * platform layer (WAMR backend keeps the buffer alive; web keeps the
       * SAB). The manager only carves it. */
    }

    bool MemoryManager::init(std::size_t region_request_bytes, MemoryIssue* out_issue) {
      auto fail = [&](MemoryIssueCode code, const char* what) {
        if (out_issue != nullptr) *out_issue = make_issue(code, what);
        shim_.trace(what);
        return false;
      };
      if (!shim_.valid()) return fail(MemoryIssueCode::ShimInvalid, "memory: shim is not fully populated");
      if (region_request_bytes < min_region_bytes) return fail(MemoryIssueCode::RegionTooSmall, "memory: region request below minimum");

      const MobagenRegionResult region = shim_.region_acquire(static_cast<std::size_t>(region_request_bytes));
      if (region.ok == 0) {
        if (out_issue != nullptr) *out_issue = make_issue(MemoryIssueCode::ShimInvalid, region.failure);
        return false;
      }
      if (region.region.bytes == nullptr || region.region.size < min_region_bytes)
        return fail(MemoryIssueCode::RegionTooSmall, "memory: shim returned an empty/undersized region");
      if (reinterpret_cast<std::uintptr_t>(region.region.bytes) % MOBAGEN_MEMORY_MIN_ALIGN != 0)
        return fail(MemoryIssueCode::RegionMisaligned, "memory: region base not 16-byte aligned");

      base_ = region.region.bytes;
      region_size_ = region.region.size;
      epoch_word_ = MobagenAllocatorWord{control_epoch_offset};
      host_park_word_ = MobagenAllocatorWord{control_park_offset};
      in_flight_word_ = MobagenAllocatorWord{control_inflight_offset};

      std::fill_n(base_, std::min<std::size_t>(region_size_, std::size_t{4096}), std::uint8_t{0});
      /* control block + handle table live in the first pages; the fill above
       * covers handle_table_end() for default slots only — clear remaining
       * headers lazily below by zeroing each chunk's blocks explicitly. */

      handle_slots_ = default_handle_slots;
      const std::uint32_t table_end = handle_table_start + handle_slots_ * handle_stride;
      if (region_size_ <= table_end) return fail(MemoryIssueCode::RegionTooSmall, "memory: region too small for handle table");
      for (std::uint32_t slot = 0; slot < handle_slots_; ++slot) {
        wr32(handle_table_start + slot * handle_stride + 0, null_offset);
        /* generation starts at 1: slot 0 with generation 0 would encode to
         * the null-handle sentinel, and no valid handle may ever be 0 */
        wr32(handle_table_start + slot * handle_stride + 4, 1);
      }

      wr32(control_magic_offset, control_magic);
      shim_.atomic_store(epoch_word_, 0);
      shim_.atomic_store(host_park_word_, 0);
      shim_.atomic_store(in_flight_word_, 0);
      wr32(control_live_offset, 0);

      /* Object area: one chunk per size class. The split gives every class
       * an EQUAL SHARE OF BYTES (therefore an equal-block carve weighted by
       * block size): churn workloads hit small classes hardest, and a
       * capacity-proportional split starves them (a 256 KiB region would
       * hand class 0 three blocks). Chunks are 16-byte aligned; the top
       * class absorbs the remainder. */
      const std::uint32_t object_area = static_cast<std::uint32_t>(region_size_) - table_end;
      const std::uint32_t share = object_area / size_class_count;
      std::uint32_t cursor = table_end;
      for (std::uint32_t c = 0; c < size_class_count; ++c) {
        std::uint32_t bytes = share & ~std::uint32_t{MOBAGEN_MEMORY_MIN_ALIGN - 1};
        if (c + 1 == size_class_count) {
          /* remainder goes to the top class: whole blocks only */
          bytes = (object_area - (cursor - table_end)) & ~std::uint32_t{MOBAGEN_MEMORY_MIN_ALIGN - 1};
        }
        const std::uint32_t block = size_class_block_bytes(c);
        const std::uint32_t blocks = bytes / block;
        classes_[c].chunk_offset = blocks > 0 ? cursor : null_offset;
        classes_[c].chunk_bytes = blocks * block;
        classes_[c].block_count = blocks;
        classes_[c].head = null_offset;
        classes_[c].free_blocks = 0;
        if (blocks > 0) {
          for (std::uint32_t b = 0; b < blocks; ++b) {
            const std::uint32_t off = cursor + b * block;
            wr32(off + 0, 0);
            wr32(off + 4, MOBAGEN_MEMORY_FLAG_FREE_BIT | c);
            wr32(off + 8, 0);
            wr32(off + 12, 0);
            wr32(off + object_header_bytes, classes_[c].head);
            classes_[c].head = off;
            ++classes_[c].free_blocks;
          }
          cursor += blocks * block;
        }
      }
      shim_.atomic_fence();
      return true;
    }

    std::uint32_t MemoryManager::rd32(std::uint32_t offset) const noexcept {
      std::uint32_t v;
      std::memcpy(&v, base_ + offset, sizeof(v));
      return v;
    }

    void MemoryManager::wr32(std::uint32_t offset, std::uint32_t v) noexcept { std::memcpy(base_ + offset, &v, sizeof(v)); }

    bool MemoryManager::park_until_epoch_even(MemoryIssue* out_issue) {
      /* Safe point 1 (allocation): block (never spin) while a GC epoch is in
       * flight, then retry from the caller. Wait until the host_park word
       * CHANGES (word_wait predicate: value != current) — re-read `cur` each
       * loop so a bumped word from the previous release can't park us on a
       * stale predicate (the hot-spin bug). */
      for (;;) {
        const std::uint32_t epoch = shim_.atomic_load(epoch_word_);
        if ((epoch & 1U) == 0U) return true;
        const std::uint32_t cur = shim_.atomic_load(host_park_word_);
        (void)shim_.word_wait(host_park_word_, cur, /*timeout_ms=*/-1);
      }
    }

    void MemoryManager::in_flight_enter() {
      /* vtable has no fetch_add: CAS-loop increment. */
      for (;;) {
        const std::uint32_t cur = shim_.atomic_load(in_flight_word_);
        if (shim_.atomic_cas(in_flight_word_, cur, cur + 1U)) return;
      }
    }

    void MemoryManager::in_flight_leave() {
      for (;;) {
        const std::uint32_t cur = shim_.atomic_load(in_flight_word_);
        if (shim_.atomic_cas(in_flight_word_, cur, cur - 1U)) {
          if (cur == 1U) shim_.word_notify_all(in_flight_word_); /* collector drain sees 0 */
          return;
        }
      }
    }

    std::uint32_t MemoryManager::alloc(std::uint32_t payload_bytes, std::uint32_t ref_count, MemoryIssue* out_issue) {
      auto fail = [&](MemoryIssueCode code, const char* what) {
        if (out_issue != nullptr) *out_issue = make_issue(code, what);
        return null_offset;
      };
      if (!park_until_epoch_even(out_issue)) return null_offset;

      if (payload_bytes > max_payload_bytes()) return fail(MemoryIssueCode::TooLarge, "memory: payload exceeds top size class");
      if (ref_count > (size_class_capacity(size_class_count - 1U) >> 2))
        return fail(MemoryIssueCode::TooLarge, "memory: ref_count exceeds payload words");

      const std::uint32_t size_class = size_class_for(payload_bytes);
      const std::uint32_t needed = ref_count * 4U;
      const std::uint32_t capacity = size_class_capacity(size_class);
      if (needed > capacity) return fail(MemoryIssueCode::TooLarge, "memory: ref words exceed size class capacity");

      for (;;) {
        std::uint32_t off = null_offset;
        {
          std::lock_guard<std::mutex> lock(host_mu_);
          SizeClassInfo& info = classes_[size_class];
          if (info.head != null_offset) {
            /* In-flight bracket around this MUTATING section: inc → re-check
             * epoch → mutate under M → dec. A collector flipping the epoch
             * mid-bracket waits for the dec (drain) instead of sweeping a
             * half-built object. */
            in_flight_enter();
            if ((shim_.atomic_load(epoch_word_) & 1U) == 0U) {
              /* Pop the head block. The free list head is read under M so a
               * racing allocator observes a consistent pop. */
              if (info.head != null_offset) {
                off = info.head;
                info.head = rd32(off + object_header_bytes);
                --info.free_blocks;

                wr32(off + 0, object_magic_v1);
                wr32(off + 4, size_class); /* clear mark + free bits */
                wr32(off + 8, payload_bytes);
                wr32(off + 12, ref_count);
                for (std::uint32_t i = 0; i < ref_count; ++i) wr32(off + object_header_bytes + i * 4U, null_handle);
                std::memset(base_ + off + object_header_bytes, 0, payload_bytes);
                wr32(control_live_offset, rd32(control_live_offset) + 1);
                in_flight_leave();
                shim_.atomic_fence();
                return off;
              }
            }
            /* Leave the bracket: epoch went odd mid-bracket (park and retry)
             * or the head emptied under us (retry the whole loop). */
            const bool epoch_odd = (shim_.atomic_load(epoch_word_) & 1U) != 0U;
            in_flight_leave();
            if (epoch_odd && !park_until_epoch_even(out_issue)) return null_offset;
            continue;
          }
        }
        /* Free list empty: allocation is itself a safe point, so run a
         * synchronous collect here — OUTSIDE the in-flight bracket (inside
         * would deadlock: the collector waits for OUR dec). */
        CollectStats stats;
        MemoryIssue collect_issue;
        if (!collect(&stats, &collect_issue)) {
          if (out_issue != nullptr) *out_issue = collect_issue;
          return null_offset;
        }
        std::lock_guard<std::mutex> lock(host_mu_);
        if (classes_[size_class].head == null_offset) return fail(MemoryIssueCode::OutOfMemory, "memory: size class free list empty after collect");
      }
    }

    void MemoryManager::free(std::uint32_t block_offset, MemoryIssue* out_issue) {
      auto fail = [&](MemoryIssueCode code, const char* what) {
        if (out_issue != nullptr) *out_issue = make_issue(code, what);
        shim_.trace(what);
      };
      if (block_offset == null_offset) return;
      std::lock_guard<std::mutex> lock(host_mu_);
      if (rd32(block_offset) != object_magic_v1 || (rd32(block_offset + 4) & MOBAGEN_MEMORY_FLAG_FREE_BIT) != 0)
        return fail(MemoryIssueCode::DoubleFree, "memory: free of foreign or already-free block");

      const std::uint32_t size_class = rd32(block_offset + 4) & MOBAGEN_MEMORY_FLAG_SIZE_CLASS_MASK;
      if (size_class >= size_class_count) return fail(MemoryIssueCode::Internal, "memory: corrupt size class");
      SizeClassInfo& info = classes_[size_class];
      wr32(block_offset + 4, MOBAGEN_MEMORY_FLAG_FREE_BIT | size_class);
      wr32(block_offset + object_header_bytes, info.head);
      info.head = block_offset;
      ++info.free_blocks;
      wr32(control_live_offset, rd32(control_live_offset) - 1);
    }

    std::uint32_t MemoryManager::handle_for(std::uint32_t block_offset, MemoryIssue* out_issue) {
      auto fail = [&](MemoryIssueCode code, const char* what) {
        if (out_issue != nullptr) *out_issue = make_issue(code, what);
        return null_handle;
      };
      if (block_offset == null_offset || rd32(block_offset) != object_magic_v1)
        return fail(MemoryIssueCode::InvalidHandle, "memory: handle_for on non-object block");

      /* Deterministic: lowest free slot; generation bumps on release so
       * stale handles never alias a new occupant. */
      std::lock_guard<std::mutex> lock(host_mu_);
      for (std::uint32_t slot = 0; slot < handle_slots_; ++slot) {
        const std::uint32_t base = handle_table_start + slot * handle_stride;
        if (rd32(base) != null_offset) continue;
        wr32(base, block_offset);
        const std::uint32_t generation = rd32(base + 4);
        ++handles_in_use_;
        return make_handle(generation, slot);
      }
      return fail(MemoryIssueCode::OutOfMemory, "memory: handle table full");
    }

    std::uint32_t MemoryManager::handle_resolve(Handle handle) const noexcept {
      const std::uint32_t slot = handle_slot(handle);
      if (slot >= handle_slots_) return null_offset;
      std::lock_guard<std::mutex> lock(host_mu_);
      const std::uint32_t base = handle_table_start + slot * handle_stride;
      /* generation word: bit31 = in-registry root flag, [30:0] = generation */
      if ((rd32(base + 4) & 0x7FFFFFFFU) != handle_generation(handle)) return null_offset;
      const std::uint32_t off = rd32(base);
      return off != null_offset && rd32(off) == object_magic_v1 ? off : null_offset;
    }

    std::uint8_t* MemoryManager::payload(std::uint32_t block_offset) noexcept { return base_ + block_offset + object_header_bytes; }

    const std::uint8_t* MemoryManager::payload(std::uint32_t block_offset) const noexcept { return base_ + block_offset + object_header_bytes; }

    std::uint32_t MemoryManager::payload_bytes(std::uint32_t block_offset) const noexcept { return rd32(block_offset + 8); }

    std::uint32_t MemoryManager::register_root(Handle handle, MemoryIssue* out_issue) {
      const std::uint32_t off = handle_resolve(handle);
      if (off == null_offset) {
        if (out_issue != nullptr) *out_issue = make_issue(MemoryIssueCode::InvalidHandle, "memory: register_root on dead handle");
        return null_handle;
      }
      const std::uint32_t slot = handle_slot(handle);
      const std::uint32_t table = handle_table_start + slot * handle_stride;
      std::lock_guard<std::mutex> lock(host_mu_);
      /* in-registry flag = bit31 of the generation word */
      wr32(table + 4, rd32(table + 4) | 0x80000000U);
      if (std::find(roots_.begin(), roots_.end(), handle) == roots_.end()) roots_.push_back(handle);
      return handle;
    }

    void MemoryManager::unregister_root(Handle handle) noexcept {
      std::lock_guard<std::mutex> lock(host_mu_);
      const auto it = std::find(roots_.begin(), roots_.end(), handle);
      if (it == roots_.end()) return;
      roots_.erase(it);
      const std::uint32_t slot = handle_slot(handle);
      if (slot >= handle_slots_) return;
      const std::uint32_t table = handle_table_start + slot * handle_stride;
      wr32(table + 4, rd32(table + 4) & ~0x80000000U);
    }

    bool MemoryManager::is_root(Handle handle) const noexcept {
      std::lock_guard<std::mutex> lock(host_mu_);
      return std::find(roots_.begin(), roots_.end(), handle) != roots_.end();
    }

    std::size_t MemoryManager::root_count() const noexcept {
      std::lock_guard<std::mutex> lock(host_mu_);
      return roots_.size();
    }

    std::uint32_t MemoryManager::root_at(std::size_t index) const noexcept {
      std::lock_guard<std::mutex> lock(host_mu_);
      return index < roots_.size() ? roots_[index] : null_handle;
    }

    void MemoryManager::mark_handle(Handle h, std::uint32_t* out_marked) {
      const std::uint32_t off = handle_resolve(h);
      if (off == null_offset) return;
      if ((rd32(off + 4) & MOBAGEN_MEMORY_FLAG_MARK_BIT) != 0) return;
      wr32(off + 4, rd32(off + 4) | MOBAGEN_MEMORY_FLAG_MARK_BIT);
      ++*out_marked;
      const std::uint32_t refs = std::min(rd32(off + 12), (size_class_capacity(size_class_count - 1U) >> 2));
      for (std::uint32_t i = 0; i < refs; ++i) {
        const Handle child = rd32(off + object_header_bytes + i * 4U);
        if (child != null_handle) mark_handle(child, out_marked);
      }
    }

    bool MemoryManager::gc_in_flight() const noexcept { return (shim_.atomic_load(epoch_word_) & 1U) != 0U; }

    std::uint32_t MemoryManager::register_guest_thread(GuestThread thread, MemoryIssue* out_issue) {
      if (thread.quiesce_fn == nullptr || thread.release_fn == nullptr) {
        if (out_issue != nullptr) *out_issue = make_issue(MemoryIssueCode::QuiesceFailed, "memory: guest thread without handshake callbacks");
        return 0;
      }
      std::lock_guard<std::mutex> lock(host_mu_);
      thread.id = next_guest_thread_id_++;
      guest_threads_.push_back(thread);
      return thread.id;
    }

    void MemoryManager::unregister_guest_thread(std::uint32_t id) noexcept {
      std::lock_guard<std::mutex> lock(host_mu_);
      const auto it = std::find_if(guest_threads_.begin(), guest_threads_.end(), [id](const GuestThread& t) { return t.id == id; });
      if (it != guest_threads_.end()) guest_threads_.erase(it);
    }

    std::size_t MemoryManager::guest_thread_count() const noexcept {
      std::lock_guard<std::mutex> lock(host_mu_);
      return guest_threads_.size();
    }

    bool MemoryManager::quiesce_guest_threads(std::uint32_t timeout_ms, CollectStats* out_stats, MemoryIssue* out_issue) {
      /* Snapshot under M: quiesce callbacks may themselves word_wait/block —
       * M must never be held across them. */
      std::vector<GuestThread> snapshot;
      {
        std::lock_guard<std::mutex> lock(host_mu_);
        snapshot = guest_threads_;
      }
      for (const GuestThread& t : snapshot) {
        const int status = t.quiesce_fn(t.user_data, static_cast<std::int32_t>(timeout_ms));
        if (status != MOBAGEN_QUIESCE_OK) {
          const char* what = status == MOBAGEN_QUIESCE_TIMEOUT
                                 ? "memory: guest thread quiesce TIMEOUT - GC aborted before marking (never mark a live heap)"
                                 : "memory: guest thread quiesce FAILED - GC aborted before marking";
          if (out_issue != nullptr)
            *out_issue = make_issue(status == MOBAGEN_QUIESCE_TIMEOUT ? MemoryIssueCode::QuiesceTimeout : MemoryIssueCode::QuiesceFailed, what);
          shim_.trace(what);
          return false;
        }
        if (out_stats != nullptr) ++out_stats->quiesced_threads;
      }
      return true;
    }

    void MemoryManager::release_guest_threads() noexcept {
      std::vector<GuestThread> snapshot;
      {
        std::lock_guard<std::mutex> lock(host_mu_);
        snapshot = guest_threads_;
      }
      for (const GuestThread& t : snapshot) t.release_fn(t.user_data);
    }

    bool MemoryManager::collect(CollectStats* out_stats, MemoryIssue* out_issue) {
      auto fail = [&](MemoryIssueCode code, const char* what) {
        if (out_issue != nullptr) *out_issue = make_issue(code, what);
        shim_.trace(what);
        return false;
      };
      if (base_ == nullptr || rd32(control_magic_offset) != control_magic) return fail(MemoryIssueCode::Internal, "memory: collect before init");

      /* Concurrent-collector detection: remember the epoch BEFORE quiescing.
       * If it differs from the epoch we finally CAS'd from, another collector
       * completed a full cycle meanwhile and its release step woke our
       * parked guests — they must be re-quiesced. */
      const std::uint32_t epoch_before = shim_.atomic_load(epoch_word_);

      /* Quiesce managed guest threads FIRST (before the epoch flip and any
       * marking). On timeout: release parked guests and abort — the epoch is
       * NEVER flipped here, so there is nothing to restore and
       * gc_in_flight() stays false (loud abort, heap untouched). */
      if (!quiesce_guest_threads(2000, out_stats, out_issue)) {
        release_guest_threads();
        return false;
      }

      /* Epoch CAS even->odd with parking retry. NO mutex held: parkers
       * observe the flip via word_wait on host_park (released at the end). */
      std::uint32_t epoch = shim_.atomic_load(epoch_word_);
      for (;;) {
        if ((epoch & 1U) != 0U) {
          /* shouldn't happen (we quiesced first) but park defensively */
          if (!park_until_epoch_even(nullptr)) {
            release_guest_threads();
            return false;
          }
          epoch = shim_.atomic_load(epoch_word_);
          continue;
        }
        if (shim_.atomic_cas(epoch_word_, epoch, epoch + 1U)) break;
        epoch = shim_.atomic_load(epoch_word_);
      }
      /* odd epoch published; bump host_park once so parked allocators' word_wait
       * predicates change; the release step bumps + notifies again. */
      shim_.atomic_store(host_park_word_, shim_.atomic_load(host_park_word_) + 1U);
      shim_.atomic_fence();

      if (epoch != epoch_before) {
        /* our guests were woken by the concurrent collector's release:
         * re-quiesce before marking or we would sweep a live heap. */
        if (!quiesce_guest_threads(2000, out_stats, out_issue)) {
          release_guest_threads();
          const std::uint32_t e = shim_.atomic_load(epoch_word_);
          shim_.atomic_store(epoch_word_, e + 1U); /* back to even */
          shim_.atomic_store(host_park_word_, shim_.atomic_load(host_park_word_) + 1U);
          shim_.word_notify_all(host_park_word_);
          return false;
        }
      }

      /* Drain any mutator inside the bracket (inc'd before the flip): wait
       * until in_flight reaches 0. Mutators decremented to 0 notify. */
      {
        std::uint32_t cur = shim_.atomic_load(in_flight_word_);
        while (cur != 0U) cur = shim_.word_wait(in_flight_word_, cur, /*timeout_ms=*/-1);
      }

      /* Mark from registered roots only (handles in the table are the
       * universe of reachable objects). */
      std::uint32_t marked = 0;
      for (const Handle root : roots_) mark_handle(root, &marked);

      /* Sweep: every allocated-but-unmarked block goes back to its class
       * free list and its handle slot is released (generation bumped). */
      std::uint32_t swept = 0;
      for (std::uint32_t c = 0; c < size_class_count; ++c) {
        const SizeClassInfo& info = classes_[c];
        if (info.chunk_offset == null_offset) continue;
        const std::uint32_t block = size_class_block_bytes(c);
        for (std::uint32_t b = 0; b < info.block_count; ++b) {
          const std::uint32_t off = info.chunk_offset + b * block;
          if (rd32(off) != object_magic_v1) continue; /* never allocated */
          const std::uint32_t flags = rd32(off + 4);
          if ((flags & MOBAGEN_MEMORY_FLAG_FREE_BIT) != 0) continue;
          if ((flags & MOBAGEN_MEMORY_FLAG_MARK_BIT) != 0) {
            wr32(off + 4, flags & ~MOBAGEN_MEMORY_FLAG_MARK_BIT); /* reset for next cycle */
            continue;
          }
          /* Unmarked live object: release every handle pointing at it. */
          for (std::uint32_t slot = 0; slot < handle_slots_; ++slot) {
            const std::uint32_t table = handle_table_start + slot * handle_stride;
            if (rd32(table) != off) continue;
            const std::uint32_t generation = rd32(table + 4) & 0x7FFFFFFFU;
            const bool was_root = (rd32(table + 4) & 0x80000000U) != 0U;
            wr32(table, null_offset);
            wr32(table + 4, generation + 1U); /* bump: stale handles die here */
            if (was_root) {
              const Handle dead = make_handle(generation, slot);
              roots_.erase(std::remove(roots_.begin(), roots_.end(), dead), roots_.end());
            }
            --handles_in_use_;
          }
          wr32(off + 4, MOBAGEN_MEMORY_FLAG_FREE_BIT | c);
          wr32(off + object_header_bytes, classes_[c].head);
          classes_[c].head = off;
          ++classes_[c].free_blocks;
          ++swept;
        }
      }
      wr32(control_live_offset, rd32(control_live_offset) >= swept ? rd32(control_live_offset) - swept : 0U);

      /* Release. Parked guest threads resume (order: unpark AFTER the heap
       * is consistent again), then host parkers are notified. */
      release_guest_threads();
      const std::uint32_t e = shim_.atomic_load(epoch_word_);
      shim_.atomic_store(epoch_word_, e + 1U); /* even again */
      shim_.atomic_store(host_park_word_, shim_.atomic_load(host_park_word_) + 1U);
      shim_.word_notify_all(host_park_word_);
      if (out_stats != nullptr) {
        out_stats->marked = marked;
        out_stats->swept = swept;
      }
      return true;
    }

    std::size_t MemoryManager::region_size() const noexcept { return region_size_; }

    std::uint32_t MemoryManager::free_count(std::uint32_t size_class) const noexcept {
      return size_class < size_class_count ? classes_[size_class].free_blocks : 0U;
    }

    std::uint32_t MemoryManager::live_objects() const noexcept { return rd32(control_live_offset); }

    std::uint8_t* MemoryManager::region_base() noexcept { return base_; }

  }  // namespace memory
}  // namespace mobagen
