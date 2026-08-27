#pragma once
// ============================================================================
// ChaseLevDeque — a lock-free work-stealing deque (Chase & Lev 2005, with the
// Lê et al. 2013 C++ memory-ordering fixes).
// ============================================================================
// The OWNER thread uses push()/pop() on the bottom (LIFO, almost never CASes).
// THIEVES use steal() on the top (one CAS). The only contended case is the last
// element, resolved by a CAS. This is the data structure that lets work-stealing
// scale: owners run uncontended; steals are rare and cheap.
//
// Fixed capacity (power of two), no dynamic growth — push() returns false when
// full and the caller falls back to a global queue. Stores non-null void*;
// pop()/steal() return nullptr for "empty or lost the race".
//
// PERFORMANCE NOTE: the seq_cst fences are mandatory for correctness on weak
// memory models; they are the price of lock-free. Owner pop()/push() touch only
// this thread's cache lines in the common path — that's why it scales.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace jobs {

  template <std::size_t Cap> class ChaseLevDeque {
    static_assert((Cap & (Cap - 1)) == 0, "Cap must be a power of two");
    static constexpr std::int64_t kCap = static_cast<std::int64_t>(Cap);
    static constexpr std::int64_t kMask = kCap - 1;

  public:
    // Owner only. false if full.
    bool push(void* x) {
      const std::int64_t b = bottom_.load(std::memory_order_relaxed);
      const std::int64_t t = top_.load(std::memory_order_acquire);
      if (b - t >= kCap) return false;  // full
      slots_[b & kMask].store(x, std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_release);
      bottom_.store(b + 1, std::memory_order_relaxed);
      return true;
    }

    // Owner only. nullptr if empty.
    void* pop() {
      const std::int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
      bottom_.store(b, std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_seq_cst);
      std::int64_t t = top_.load(std::memory_order_relaxed);
      if (t > b) {  // empty
        bottom_.store(b + 1, std::memory_order_relaxed);
        return nullptr;
      }
      void* x = slots_[b & kMask].load(std::memory_order_relaxed);
      if (t == b) {                                                                                                      // last element: race a thief
        if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) x = nullptr;  // a thief won
        bottom_.store(b + 1, std::memory_order_relaxed);
      }
      return x;
    }

    // Thieves. nullptr if empty or lost the race.
    void* steal() {
      std::int64_t t = top_.load(std::memory_order_acquire);
      std::atomic_thread_fence(std::memory_order_seq_cst);
      const std::int64_t b = bottom_.load(std::memory_order_acquire);
      if (t >= b) return nullptr;  // empty
      void* x = slots_[t & kMask].load(std::memory_order_relaxed);
      if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) return nullptr;  // lost the race
      return x;
    }

  private:
    std::atomic<std::int64_t> top_{0};
    std::atomic<std::int64_t> bottom_{0};
    std::atomic<void*> slots_[Cap];
  };

}  // namespace jobs
