#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "memory/memory_manager.hpp"
#include "memory/native_shim.hpp"
#include "memory/object_header.h"
#include "memory/shim.hpp"

#if defined(MOBAGEN_TEST_WAMR_NATIVE_SHIM)
#include "plugins/wamr_backend.hpp"
#endif

using mobagen::memory::NativeShim;
using Handle = mobagen::memory::MemoryManager::Handle;

TEST_CASE("native shim: standalone region_acquire is ok and 16-byte aligned, grows to cover minimum") {
  auto native = mobagen::memory::make_native_shim();
  auto shim = native->shim();
  REQUIRE(shim.valid());

  auto r = shim.region_acquire(64 * 1024);
  REQUIRE(r.ok == 1);
  CHECK(r.region.bytes != nullptr);
  CHECK(r.region.size >= 64 * 1024);
  CHECK(reinterpret_cast<std::uintptr_t>(r.region.bytes) % 16 == 0);
  CHECK(r.word.opaque == 0);

  /* growth: minimum above the initial 256 KiB is honored (64 KiB-rounded) */
  auto grown = shim.region_acquire(512 * 1024);
  REQUIRE(grown.ok == 1);
  CHECK(grown.region.size >= 512 * 1024);
  CHECK(grown.region.size % (64 * 1024) == 0);
  CHECK(reinterpret_cast<std::uintptr_t>(grown.region.bytes) % 16 == 0);
  /* spec: free old, alloc new — base may move across growth; no stability assert */
  CHECK(grown.region.bytes != nullptr);

  auto again = shim.region_acquire(4 * 1024);
  REQUIRE(again.ok == 1);
  CHECK(again.region.bytes == grown.region.bytes);
  CHECK(again.region.size == grown.region.size);
  REQUIRE(again.ok == 1);
  CHECK(again.region.bytes == grown.region.bytes);
  CHECK(again.region.size == grown.region.size);
}

TEST_CASE("native shim: atomic load/store/cas round-trip on region word; fence callable") {
  auto native = mobagen::memory::make_native_shim();
  auto shim = native->shim();
  auto r = shim.region_acquire(256);
  REQUIRE(r.ok == 1);
  MobagenAllocatorWord w = r.word;

  shim.atomic_store(w, 0);
  CHECK(shim.atomic_load(w) == 0);
  shim.atomic_store(w, 7);
  CHECK(shim.atomic_load(w) == 7);

  CHECK(shim.atomic_cas(w, 7, 9));
  CHECK(shim.atomic_load(w) == 9);
  CHECK_FALSE(shim.atomic_cas(w, 7, 42));
  CHECK(shim.atomic_load(w) == 9);

  shim.atomic_fence(); /* must simply be callable */
  CHECK(shim.atomic_load(w) == 9);
}

TEST_CASE("native shim: foreign region too small fails with the loud message") {
  alignas(16) std::array<std::uint8_t, 256> small{};
  auto native = mobagen::memory::make_native_shim(small.data(), small.size());
  auto shim = native->shim();
  REQUIRE(shim.valid());

  auto r = shim.region_acquire(512);
  REQUIRE(r.ok == 0);
  CHECK(std::string(r.failure) ==
        "native shim: foreign region (256 bytes) smaller than requested minimum 512");
  CHECK(native->trace_log().size() >= 1);
}

TEST_CASE("native shim: foreign region (WAMR shared heap shape) acquires in place, never freed") {
  static constexpr std::size_t kForeign = 256U * 1024U;
  alignas(16) static std::uint8_t foreign[kForeign];
  auto native = mobagen::memory::make_native_shim(foreign, kForeign);
  auto shim = native->shim();
  REQUIRE(shim.valid());

  auto r = shim.region_acquire(64 * 1024);
  REQUIRE(r.ok == 1);
  CHECK(r.region.bytes == foreign);
  CHECK(r.region.size == kForeign);
  CHECK(reinterpret_cast<std::uintptr_t>(r.region.bytes) % 16 == 0);
  CHECK(r.word.opaque == 0);

  /* atomics work directly on the borrowed bytes */
  shim.atomic_store(r.word, 3);
  CHECK(shim.atomic_load(r.word) == 3);
  CHECK(foreign[0] == 3);
}

TEST_CASE("native shim: word_wait parks until word_notify_all wakes it with the new value") {
  auto native = mobagen::memory::make_native_shim();
  auto shim = native->shim();
  auto r = shim.region_acquire(64);
  REQUIRE(r.ok == 1);
  MobagenAllocatorWord w = r.word;
  shim.atomic_store(w, 0);

  std::uint32_t observed = 999;
  std::thread waiter([&] { observed = shim.word_wait(w, 0, 2000); });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  shim.atomic_store(w, 1);
  shim.word_notify_all(w);

  waiter.join();
  CHECK(observed == 1);
  CHECK(shim.atomic_load(w) == 1);
}

TEST_CASE("native shim: word_wait times out after ~timeout on unchanged word, returns observed") {
  auto native = mobagen::memory::make_native_shim();
  auto shim = native->shim();
  auto r = shim.region_acquire(64);
  REQUIRE(r.ok == 1);
  MobagenAllocatorWord w = r.word;
  shim.atomic_store(w, 5);

  auto t0 = shim.now_ns();
  std::uint32_t observed = shim.word_wait(w, 5, 80);
  auto t1 = shim.now_ns();
  CHECK(observed == 5);
  CHECK(t1 - t0 >= 70'000'000ULL); /* ~80ms budgeted, 70ms floor for jitter */
  CHECK(t1 - t0 < 3'000'000'000ULL);
}

TEST_CASE("native shim: now_ns monotonic across calls") {
  auto native = mobagen::memory::make_native_shim();
  auto shim = native->shim();
  auto a = shim.now_ns();
  auto b = shim.now_ns();
  auto c = shim.now_ns();
  CHECK(b >= a);
  CHECK(c >= b);
}

TEST_CASE("native shim: trace collects messages") {
  auto native = mobagen::memory::make_native_shim();
  auto shim = native->shim();
  shim.trace("hello");
  shim.trace("loud failure");
  REQUIRE(native->trace_log().size() == 2);
  CHECK(native->trace_log()[0] == "hello");
  CHECK(native->trace_log()[1] == "loud failure");
}

/* ==========================================================================
 * todo 13 micro-task C: concurrency / managed-guest / quiesce-timeout
 * scenarios over the REAL NativeShim + MemoryManager (standalone region).
 * ==========================================================================
 *
 * Managed-guest loop protocol (what the todo 13 platform glue wraps around
 * mobagen_module_thread_quiesce_v1):
 *   guest:   check request flag -> park on OWN word -> release -> alloc -> ...
 *   parking: word 0 of the payload of a ROOTED 16-byte block
 *            (park_word = block_off + object_header_bytes), park via
 *            shim.word_wait(park_word, cur, -1)
 *   release: bump the word value + word_notify_all
 *
 * Request flag and parking word are SEQUENCES, not bools: each quiesce_fn
 * bumps the request sequence; the guest parks once per observed request;
 * release_fn bumps the parking word exactly once per granted quiesce.
 * A bool flag would let a concurrent host collect and the guest's own
 * auto-collect quiesce steal each other's requests.
 */

namespace {

  constexpr std::int32_t kGuestQuiesceBudgetMs = 600; /* < manager's 2000 */

  struct ManagedGuestLoop {
    mobagen::memory::Shim shim{};
    std::uint32_t park_word{0};
    std::atomic<bool> stop{false};
    std::atomic<std::thread::id> guest_thread_id{};
    std::atomic<std::uint32_t> request_seq{0}; /* quiesce_fn ++, guest consumes */
    /* guest stores `requested` here BEFORE parking: the host-side quiesce_fn
     * polls this to learn the guest reached its parking word (release_fn only
     * runs after quiesce returns OK, so the park word itself can't signal it) */
    std::atomic<std::uint32_t> parked_seq{0};
    /* true while the guest is inside mgr->alloc(): allocation is safe point
     * #1 (the collector's epoch flip parks it at the barrier), so quiesce may
     * treat an in-alloc guest as parked — a slow inner auto-collect under TSan
     * would otherwise hold the guest past the 600ms park deadline */
    std::atomic<bool> in_alloc{false};

    std::atomic<int> allocs{0};
    std::atomic<int> quiesce_calls{0};
    std::atomic<int> quiesce_oks{0};
    std::atomic<int> quiesce_timeouts{0};
    std::atomic<int> release_calls{0};
    std::atomic<int> self_quiesce_hits{0};

    mobagen::memory::MemoryManager* mgr{nullptr};

    void run() {
      guest_thread_id.store(std::this_thread::get_id());
      std::uint32_t seen_requests = request_seq.load(std::memory_order_acquire);
      while (!stop.load(std::memory_order_relaxed)) {
        /* Drain pending requests: park once per request. A park whose
         * release already happened returns immediately (word moved), so a
         * release bump racing the parking read can never strand a stale
         * request (lost-wakeup fix: re-read until caught up). */
        for (;;) {
          const std::uint32_t requested = request_seq.load(std::memory_order_acquire);
          if (requested == seen_requests) break;
          parked_seq.store(requested, std::memory_order_release);
          park_self();
          seen_requests = requested;
        }
        const std::uint32_t off = [&] {
          in_alloc.store(true, std::memory_order_release);
          const std::uint32_t o = mgr->alloc(16, 0);
          in_alloc.store(false, std::memory_order_release);
          return o;
        }();
        if (off == mobagen::memory::null_offset) break;
        allocs.fetch_add(1, std::memory_order_relaxed);
      }
    }

    /* last park-word value this guest CONSUMED (starts at the setup value).
     * Parking waits for the word to leave word_base — the predicate comes
     * from the guest's own history, NEVER re-read from the word: a release
     * bump landing between parked_seq.store and word_wait entry would
     * otherwise park the guest on an already-bumped value nobody rewrites
     * (lost wakeup; word_wait returns the observed value, resyncing it). */
    std::uint32_t word_base{0};

    void park_self() {
      const MobagenAllocatorWord w{park_word};
      word_base = shim.word_wait(w, word_base, -1);
    }

    static int quiesce_fn(void* user_data, std::int32_t timeout_ms) {
      auto* self = static_cast<ManagedGuestLoop*>(user_data);
      ++self->quiesce_calls;
      if (std::this_thread::get_id() == self->guest_thread_id.load()) {
        /* SELF-QUIESCE RULE: the collector IS this thread (its own alloc's
         * auto-collect path) — the thread is at a safe point by construction
         * (inside alloc), so it reports parked immediately instead of
         * deadlocking waiting for itself. */
        ++self->self_quiesce_hits;
        ++self->quiesce_oks;
        return MOBAGEN_QUIESCE_OK;
      }
      /* Request one park; the guest announces reaching its parking word by
       * storing the request number into parked_seq (release_fn only writes
       * the park word AFTER we return OK, so it cannot be the signal). */
      const std::uint32_t requested = self->request_seq.fetch_add(1, std::memory_order_acq_rel) + 1U;
      /* the guest's OWN 600ms deadline — strictly under the manager's 2000ms
       * budget so a stuck guest surfaces as this side's TIMEOUT */
      const std::uint64_t deadline = self->shim.now_ns() + 1000000ULL * static_cast<std::uint64_t>(kGuestQuiesceBudgetMs);
      while (self->shim.now_ns() < deadline) {
        const std::uint32_t parked_now = self->parked_seq.load(std::memory_order_acquire);
        /* quiesced = parked for THIS request, asleep after consuming the
         * PREVIOUS release (word_wait with a post-bump predicate), or inside
         * alloc (safe point #1: the epoch flip parks it at the barrier). The
         * asleep case must NOT match requested==1 — that is the ignoring
         * guest, which never parks at all. */
        const bool parked_this = parked_now >= requested;
        const bool asleep_after_prev_release = requested > 1U && parked_now + 1U == requested;
        if (parked_this || asleep_after_prev_release || self->in_alloc.load(std::memory_order_acquire)) {
          ++self->quiesce_oks;
          return MOBAGEN_QUIESCE_OK;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      ++self->quiesce_timeouts;
      return MOBAGEN_QUIESCE_TIMEOUT;
    }

    static void release_fn(void* user_data) {
      auto* self = static_cast<ManagedGuestLoop*>(user_data);
      ++self->release_calls;
      const MobagenAllocatorWord w{self->park_word};
      self->shim.atomic_store(w, self->shim.atomic_load(w) + 1U);
      self->shim.word_notify_all(w);
    }
  };

}  // namespace

TEST_CASE("native shim: two threads allocate concurrently through one manager and host collect completes") {
  auto native = mobagen::memory::make_native_shim();
  auto shim = native->shim();
  REQUIRE(shim.valid());
  mobagen::memory::MemoryManager mgr(shim);
  REQUIRE(mgr.init(256U * 1024U));

  std::atomic<int> allocs_a{0};
  std::atomic<int> allocs_b{0};
  std::atomic<bool> collect_done{false};
  const auto churn = [&mgr](std::atomic<int>* counter, const std::atomic<bool>* done) {
    while (!done->load(std::memory_order_relaxed)) {
      const std::uint32_t off = mgr.alloc(16, 0);
      if (off == mobagen::memory::null_offset) return;
      counter->fetch_add(1, std::memory_order_relaxed);
    }
  };

  std::thread ta(churn, &allocs_a, &collect_done);
  std::thread tb(churn, &allocs_b, &collect_done);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  mobagen::memory::CollectStats stats;
  mobagen::memory::MemoryIssue issue;
  REQUIRE(mgr.collect(&stats, &issue));
  collect_done.store(true, std::memory_order_relaxed);
  ta.join();
  tb.join();

  CHECK(allocs_a.load() > 0);
  CHECK(allocs_b.load() > 0);
  CHECK_FALSE(mgr.gc_in_flight());
  std::uint32_t in_flight = 0;
  std::memcpy(&in_flight, mgr.region_base() + 16U, sizeof(in_flight));
  CHECK(in_flight == 0);
  /* churn threads may have allocated between collect() and their join, so
   * leftover unrooted objects at this point are expected; the final cycle
   * below (threads joined, nothing rooted) is where live must hit 0 */
  REQUIRE(mgr.collect(&stats, &issue)); /* clean second cycle */
  CHECK(mgr.live_objects() == 0);
}

TEST_CASE("native shim: managed guest parks on its own rooted word across cycles; self-quiesce rule fires on auto-collect") {
  auto native = mobagen::memory::make_native_shim();
  auto shim = native->shim();
  REQUIRE(shim.valid());
  mobagen::memory::MemoryManager mgr(shim);
  REQUIRE(mgr.init(256U * 1024U));

  const std::uint32_t block_off = mgr.alloc(16, 1);
  REQUIRE(block_off != mobagen::memory::null_offset);
  const Handle root = mgr.register_root(mgr.handle_for(block_off));
  REQUIRE(root != mobagen::memory::null_handle);

  ManagedGuestLoop guest;
  guest.shim = shim;
  guest.park_word = block_off + mobagen::memory::object_header_bytes;
  guest.mgr = &mgr;
  shim.atomic_store(MobagenAllocatorWord{guest.park_word}, 0);

  std::thread guest_thread([&guest] { guest.run(); });
  while (guest.guest_thread_id.load() == std::thread::id{}) std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  mobagen::memory::GuestThread reg{};
  reg.user_data = &guest;
  reg.quiesce_fn = &ManagedGuestLoop::quiesce_fn;
  reg.release_fn = &ManagedGuestLoop::release_fn;
  REQUIRE(mgr.register_guest_thread(reg) != 0);

  mobagen::memory::CollectStats stats;
  mobagen::memory::MemoryIssue issue;

  /* host collect() cycles 1-3 while the guest churns garbage */
  for (int cycle = 0; cycle < 3; ++cycle) {
    REQUIRE(mgr.collect(&stats, &issue));
    CHECK_FALSE(mgr.gc_in_flight());
    CHECK(mgr.handle_resolve(root) != mobagen::memory::null_offset);
    CHECK(stats.quiesced_threads >= 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  CHECK(guest.quiesce_calls.load() >= 3);
  CHECK(guest.release_calls.load() >= 3);
  CHECK(guest.quiesce_timeouts.load() == 0);
  CHECK(guest.allocs.load() > 0);

  /* Force the guest's auto-collect: drain the class-0 free list to ONE
   * block host-side (host allocs keep succeeding without collecting while
   * the list is non-empty), then hand the empty list to the guest: its
   * next alloc() runs collect() ON THE GUEST THREAD -> quiesce_fn there ->
   * SELF-QUIESCE RULE. The guest's garbage is unrooted, so the auto-collect
   * frees its blocks and allocation resumes. */
  std::uint32_t drained = 0;
  for (;;) {
    const std::uint32_t free_now = mgr.free_count(0);
    if (free_now <= 1U) break;
    const std::uint32_t off = mgr.alloc(16, 0);
    if (off == mobagen::memory::null_offset) break;
    ++drained;
  }
  CHECK(drained > 0U);
  const std::uint64_t deadline = shim.now_ns() + 10'000'000'000ULL;
  while (guest.self_quiesce_hits.load() < 1 && shim.now_ns() < deadline) std::this_thread::yield();
  CHECK(guest.self_quiesce_hits.load() >= 1);

  /* quiesce_cycles = completed handshakes (host parks + self-quiesces) */
  CHECK(guest.quiesce_oks.load() >= 2);
  CHECK(guest.allocs.load() > 0);

  CHECK(mgr.handle_resolve(root) != mobagen::memory::null_offset);

  /* teardown: unregister, wake the guest if parked (harmless extra bump),
   * stop, join, then a final real collect succeeds. Epoch / in_flight checks
   * live AFTER the join + final collect: the guest's own auto-collect cycles
   * make any mid-flight observation here inherently racy. */
  mgr.unregister_guest_thread(1U);
  ManagedGuestLoop::release_fn(&guest);
  guest.stop.store(true, std::memory_order_relaxed);
  guest_thread.join();
  REQUIRE(mgr.collect(&stats, &issue));
  CHECK(stats.marked >= 1); /* the rooted parking block survives */
  CHECK_FALSE(mgr.gc_in_flight()); /* epoch even at rest */
  std::uint32_t in_flight = 0;
  std::memcpy(&in_flight, mgr.region_base() + 16U, sizeof(in_flight));
  CHECK(in_flight == 0);
}

TEST_CASE("native shim: quiesce-ignoring guest times out; collect aborts loudly, heap untouched, then recovers") {
  auto native = mobagen::memory::make_native_shim();
  auto shim = native->shim();
  REQUIRE(shim.valid());
  mobagen::memory::MemoryManager mgr(shim);
  REQUIRE(mgr.init(128U * 1024U));

  const std::uint32_t block_off = mgr.alloc(16, 1);
  REQUIRE(block_off != mobagen::memory::null_offset);
  const Handle root = mgr.register_root(mgr.handle_for(block_off));
  REQUIRE(root != mobagen::memory::null_handle);

  ManagedGuestLoop guest;
  guest.shim = shim;
  guest.park_word = block_off + mobagen::memory::object_header_bytes;
  guest.mgr = &mgr;
  shim.atomic_store(MobagenAllocatorWord{guest.park_word}, 0);

  /* ignoring guest: spins without ever checking the request flag — exactly
   * a guest whose loop has no safe-point check */
  std::atomic<bool> stop_ignorer{false};
  std::thread guest_thread([&] {
    guest.guest_thread_id.store(std::this_thread::get_id());
    while (!stop_ignorer.load(std::memory_order_relaxed)) std::this_thread::yield();
  });
  while (guest.guest_thread_id.load() == std::thread::id{}) std::this_thread::yield();

  mobagen::memory::GuestThread reg{};
  reg.user_data = &guest;
  reg.quiesce_fn = &ManagedGuestLoop::quiesce_fn;
  reg.release_fn = &ManagedGuestLoop::release_fn;
  REQUIRE(mgr.register_guest_thread(reg) != 0);

  const std::uint32_t live_before = mgr.live_objects();
  const std::size_t trace_before = native->trace_log().size();

  mobagen::memory::CollectStats stats;
  mobagen::memory::MemoryIssue issue;
  const auto t0 = shim.now_ns();
  CHECK_FALSE(mgr.collect(&stats, &issue)); /* quiesce_fn's 600ms budget < manager's 2000 */
  const auto t1 = shim.now_ns();
  CHECK(issue.code == mobagen::memory::MemoryIssueCode::QuiesceTimeout);
  CHECK(t1 - t0 >= 500'000'000ULL); /* it really waited out the guest deadline */
  REQUIRE(native->trace_log().size() > trace_before);
  bool found_timeout = false;
  for (std::size_t i = trace_before; i < native->trace_log().size(); ++i) {
    if (native->trace_log()[i].find("quiesce TIMEOUT") != std::string::npos) found_timeout = true;
  }
  CHECK(found_timeout);
  CHECK(guest.quiesce_timeouts.load() == 1);
  CHECK(guest.self_quiesce_hits.load() == 0);
  CHECK_FALSE(mgr.gc_in_flight()); /* epoch never flipped */
  CHECK(mgr.live_objects() == live_before); /* heap untouched */
  CHECK(mgr.handle_resolve(root) != mobagen::memory::null_offset);

  mgr.unregister_guest_thread(1U);
  stop_ignorer.store(true, std::memory_order_relaxed);
  guest_thread.join();
  REQUIRE(mgr.collect(&stats, &issue));
  CHECK(stats.marked >= 1);
  CHECK(mgr.handle_resolve(root) != mobagen::memory::null_handle);
}

#if defined(MOBAGEN_TEST_WAMR_NATIVE_SHIM)
TEST_CASE("native shim: foreign region over the real WAMR shared heap (alloc/handle/root/collect/resolve)") {
  constexpr std::uint32_t heap_bytes = 64U * 1024U;
  mobagen::plugins::WamrBackend backend({.shared_heap_size_bytes = heap_bytes});
  REQUIRE(backend.available());
  const auto region = backend.shared_heap_region();
  REQUIRE(region.data != nullptr);
  REQUIRE(region.size >= heap_bytes);

  auto native = mobagen::memory::make_native_shim(region.data, region.size);
  auto shim = native->shim();
  REQUIRE(shim.valid());
  mobagen::memory::MemoryManager mgr(shim);
  REQUIRE(mgr.init(region.size));

  const std::uint32_t off = mgr.alloc(24, 1);
  REQUIRE(off != mobagen::memory::null_offset);
  const Handle h = mgr.register_root(mgr.handle_for(off));
  REQUIRE(h != mobagen::memory::null_handle);
  auto* payload = reinterpret_cast<std::uint32_t*>(mgr.payload(mgr.handle_resolve(h)));
  payload[1] = 0xC0FFEEU;

  mobagen::memory::CollectStats stats;
  mobagen::memory::MemoryIssue issue;
  REQUIRE(mgr.collect(&stats, &issue));
  CHECK(stats.marked == 1);
  const std::uint32_t resolved = mgr.handle_resolve(h);
  REQUIRE(resolved != mobagen::memory::null_offset);
  const auto* after = reinterpret_cast<const std::uint32_t*>(mgr.payload(resolved));
  CHECK(after[1] == 0xC0FFEEU); /* bytes came from the real WAMR heap */
  CHECK_FALSE(mgr.gc_in_flight());
}
#endif
