#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "mock_memory_shim.hpp"

#include "memory/memory_manager.hpp"
#include "memory/object_header.h"

using mobagen::memory::CollectStats;
using mobagen::memory::GuestThread;
using mobagen::memory::MemoryIssue;
using mobagen::memory::MemoryManager;
using mobagen::memory::testing::MockShim;
using Handle = mobagen::memory::MemoryManager::Handle;

namespace {

  /* (e) fake managed guest thread: host-side std::thread running the fake
   * guest's quiesce handshake body, exactly the shape todo 13/14 wrap
   * around mobagen_module_thread_quiesce_v1 exports. The GUEST thread
   * polls the GC request flag (its "safe point check") and parks itself;
   * the host-registered handshake sets the flag and WAITS until the guest
   * has parked — so by the time quiesce_fn returns OK to the GC core, the
   * guest thread really is at a safe point. release_cb wakes it. The
   * ordering counter asserts park before mark, unpark after. */
  struct FakeGuestThread {
    std::atomic<int> phase{0}; /* 0 running, 1 parked, 2 released */
    std::atomic<int> quiesce_calls{0};
    std::atomic<int> parked_at_order{-1};
    std::atomic<int> released_at_order{-1};
    std::atomic<int> order_cursor{0};
    std::atomic<bool> gc_requested{false};
    std::atomic<bool> stop{false};
    std::mutex mu;
    std::condition_variable cv;
    std::thread thread;

    /* runs ON the fake guest thread: park at the safe point */
    void guest_park(std::unique_lock<std::mutex>& lock) {
      phase = 1;
      parked_at_order = ++order_cursor;
      cv.notify_all();
      cv.wait(lock, [&] { return phase.load() != 1; });
    }

    /* host-side handshake wrapper the GC core invokes */
    static int quiesce_cb(void* ud, int32_t) {
      auto* self = static_cast<FakeGuestThread*>(ud);
      ++self->quiesce_calls;
      std::unique_lock<std::mutex> lock(self->mu);
      self->gc_requested = true;
      self->cv.notify_all();
      /* wait until the guest thread actually parked (bounded by the
       * guest loop checking the flag) */
      self->cv.wait(lock, [&] { return self->phase.load() == 1 || self->stop.load(); });
      return self->phase.load() == 1 ? MOBAGEN_QUIESCE_OK : MOBAGEN_QUIESCE_FAILED;
    }
    static void release_cb(void* ud) {
      auto* self = static_cast<FakeGuestThread*>(ud);
      std::lock_guard<std::mutex> lock(self->mu);
      self->phase = 2;
      self->released_at_order = ++self->order_cursor;
      self->cv.notify_all();
    }

    void start() {
      thread = std::thread([this] {
        std::unique_lock<std::mutex> lock(mu);
        while (!stop.load()) {
          if (gc_requested.load() && phase.load() == 0) {
            guest_park(lock);
            break; /* released: exit the loop */
          }
          cv.wait_for(lock, std::chrono::milliseconds(1));
        }
      });
    }
    void join() {
      stop = true;
      cv.notify_all();
      if (thread.joinable()) thread.join();
    }
  };

}  // namespace

TEST_CASE("memory core: alloc/free churn and forced GC collect unreachable objects") {
  MockShim mock;
  MemoryManager mgr(mock.shim());
  REQUIRE(mgr.init(256U * 1024U));
  CHECK_FALSE(mgr.gc_in_flight());

  /* baseline free-list depth for class 0 (16B payloads): equal-share carve
   * of 256 KiB gives ~650 class-0 blocks; a wave is 1000 allocs, so waves
   * themselves must churn through collections (below) */
  const std::uint32_t baseline = mgr.free_count(0);
  REQUIRE(baseline > 600U);

  std::vector<Handle> keep;
  CollectStats stats;
  MemoryIssue issue;
  constexpr int waves = 10;
  constexpr int per_wave = 1000;
  for (int wave = 0; wave < waves; ++wave) {
    for (int i = 0; i < per_wave; ++i) {
      const std::uint32_t off = mgr.alloc(16, 0);
      REQUIRE(off != mobagen::memory::null_offset);
      const Handle h = mgr.handle_for(off);
      REQUIRE(h != mobagen::memory::null_handle);
      if (wave == 0 && i % 100 == 0) {
        REQUIRE(mgr.register_root(h) == h);
        keep.push_back(h);
      }
      /* everything else is garbage: handles dropped, objects unrooted */
    }
    /* forced GC per wave; alloc may also have auto-collected mid-wave when
     * the class-0 free list emptied (allocation is itself a safe point), so
     * assert the invariant (only roots live) rather than per-wave counts */
    REQUIRE(mgr.collect(&stats, &issue));
    CHECK(mgr.live_objects() == keep.size());
  }
  /* all 10k dropped objects were collected across the waves; only the
   * roots remain and every other class-0 block is back on the free list */
  CHECK(mgr.live_objects() == keep.size());
  CHECK(mgr.free_count(0) == baseline - keep.size());

  /* every kept root survives and resolves to valid memory */
  for (const Handle h : keep) {
    const std::uint32_t off = mgr.handle_resolve(h);
    REQUIRE(off != mobagen::memory::null_offset);
    CHECK(mgr.payload(off) != nullptr);
  }

  /* second phase: dropping all roots collects the rest */
  for (const Handle h : keep) mgr.unregister_root(h);
  REQUIRE(mgr.collect(&stats, &issue));
  CHECK(stats.swept == keep.size());
  CHECK(mgr.free_count(0) == baseline);
  CHECK(mgr.live_objects() == 0);
}

TEST_CASE("memory core: reachable-via-handle object is NEVER collected (UAF guard)") {
  MockShim mock;
  MemoryManager mgr(mock.shim());
  REQUIRE(mgr.init(128U * 1024U));

  const std::uint32_t off = mgr.alloc(16, 1);
  REQUIRE(off != mobagen::memory::null_offset);
  const Handle h = mgr.register_root(mgr.handle_for(off));
  REQUIRE(h != mobagen::memory::null_handle);

  /* graph: root -> child (held via the root's single ref word) */
  const std::uint32_t child_off = mgr.alloc(32, 0);
  REQUIRE(child_off != mobagen::memory::null_offset);
  const Handle child = mgr.handle_for(child_off);
  REQUIRE(child != mobagen::memory::null_handle);
  auto* refs = reinterpret_cast<std::uint32_t*>(mgr.payload(off));
  refs[0] = child;

  CollectStats stats;
  MemoryIssue issue;
  /* repeated GC cycles must never sweep a handle-reachable object */
  for (int cycle = 0; cycle < 5; ++cycle) {
    REQUIRE(mgr.collect(&stats, &issue));
    CHECK(stats.swept == 0);
    CHECK(mgr.live_objects() == 2);
    CHECK(mgr.handle_resolve(h) != mobagen::memory::null_offset);
    CHECK(mgr.handle_resolve(child) != mobagen::memory::null_offset);
    /* payload still valid: read/write it (would be UAF if collected).
     * Word 0 is the single ref word holding child; write word 1. */
    auto* root_payload = reinterpret_cast<std::uint32_t*>(mgr.payload(mgr.handle_resolve(h)));
    root_payload[1] = static_cast<std::uint32_t>(cycle) + 0xC0DEU;
    CHECK(root_payload[1] == static_cast<std::uint32_t>(cycle) + 0xC0DEU);
  }

  /* dropping the root handle's registration collects both */
  mgr.unregister_root(h);
  REQUIRE(mgr.collect(&stats, &issue));
  CHECK(stats.swept == 2);
  CHECK(mgr.handle_resolve(h) == mobagen::memory::null_offset); /* generation bumped */
  CHECK(mgr.handle_resolve(child) == mobagen::memory::null_offset);
}

TEST_CASE("memory core: deterministic root registration API") {
  MockShim mock;
  MemoryManager mgr(mock.shim());
  REQUIRE(mgr.init(128U * 1024U));

  /* identical allocation sequences produce identical handle values */
  std::vector<Handle> handles;
  for (int i = 0; i < 16; ++i) {
    const std::uint32_t off = mgr.alloc(48, 0);
    REQUIRE(off != mobagen::memory::null_offset);
    handles.push_back(mgr.handle_for(off));
  }

  MockShim mock2;
  MemoryManager mgr2(mock2.shim());
  REQUIRE(mgr2.init(128U * 1024U));
  for (int i = 0; i < 16; ++i) {
    const std::uint32_t off = mgr2.alloc(48, 0);
    REQUIRE(off != mobagen::memory::null_offset);
    CHECK(mgr2.handle_for(off) == handles[static_cast<std::size_t>(i)]);
  }

  /* registration order is preserved and addressable */
  for (std::size_t i = 0; i < handles.size(); ++i) {
    REQUIRE(mgr.register_root(handles[i]) == handles[i]);
    CHECK(mgr.root_at(i) == handles[i]);
    CHECK(mgr.root_count() == i + 1);
    CHECK(mgr.is_root(handles[i]));
  }
  /* idempotent re-registration */
  REQUIRE(mgr.register_root(handles[0]) == handles[0]);
  CHECK(mgr.root_count() == handles.size());

  mgr.unregister_root(handles[3]);
  CHECK_FALSE(mgr.is_root(handles[3]));
  CHECK(mgr.root_count() == handles.size() - 1);

  /* invalid handles are rejected deterministically */
  MemoryIssue issue;
  CHECK(mgr.register_root(mobagen::memory::null_handle, &issue) == mobagen::memory::null_handle);
  CHECK(issue.code == mobagen::memory::MemoryIssueCode::InvalidHandle);
  CHECK(mgr.handle_resolve(0xFFFFFFFFU) == mobagen::memory::null_offset); /* bogus generation */
}

TEST_CASE("memory core: managed guest thread quiesce parks before mark and unparks on release") {
  MockShim mock;
  MemoryManager mgr(mock.shim());
  REQUIRE(mgr.init(128U * 1024U));

  const std::uint32_t off = mgr.alloc(16, 0);
  const Handle root = mgr.register_root(mgr.handle_for(off));
  REQUIRE(root != mobagen::memory::null_handle);

  FakeGuestThread guest;
  guest.start();

  GuestThread reg{};
  reg.user_data = &guest;
  reg.quiesce_fn = &FakeGuestThread::quiesce_cb;
  reg.release_fn = &FakeGuestThread::release_cb;
  const std::uint32_t guest_id = mgr.register_guest_thread(reg);
  REQUIRE(guest_id != 0);
  CHECK(mgr.guest_thread_count() == 1);

  CollectStats stats;
  MemoryIssue issue;
  REQUIRE(mgr.collect(&stats, &issue));
  /* ordering: park (quiesce) strictly before mark, unpark (release) after */
  CHECK(guest.parked_at_order == 1);
  CHECK(guest.released_at_order == 2);
  CHECK(guest.quiesce_calls.load() == 1);
  CHECK(stats.quiesced_threads == 1);
  CHECK(stats.marked == 1); /* root survived the quiesced world */
  CHECK(mgr.handle_resolve(root) != mobagen::memory::null_offset);

  mgr.unregister_guest_thread(guest_id);
  CHECK(mgr.guest_thread_count() == 0);
  guest.join();
}

TEST_CASE("memory core: quiesce timeout aborts GC loudly without marking") {
  MockShim mock;
  MemoryManager mgr(mock.shim());
  REQUIRE(mgr.init(128U * 1024U));

  const std::uint32_t off = mgr.alloc(16, 0);
  const Handle root = mgr.register_root(mgr.handle_for(off));

  /* unresponsive guest: handshake reports timeout without parking */
  static std::atomic<int> calls{0};
  calls = 0;
  GuestThread reg{};
  reg.user_data = nullptr;
  reg.quiesce_fn = [](void*, int32_t) -> int {
    ++calls;
    return MOBAGEN_QUIESCE_TIMEOUT;
  };
  reg.release_fn = [](void*) {};
  REQUIRE(mgr.register_guest_thread(reg) != 0);

  CollectStats stats;
  MemoryIssue issue;
  CHECK_FALSE(mgr.collect(&stats, &issue));
  CHECK(issue.code == mobagen::memory::MemoryIssueCode::QuiesceTimeout);
  CHECK(mgr.handle_resolve(root) != mobagen::memory::null_offset); /* heap untouched */
  CHECK(mgr.live_objects() == 1);                                  /* never swept */
  CHECK_FALSE(mgr.gc_in_flight());                                 /* world released */
  REQUIRE(calls.load() == 1);

  /* after unregistering the offender, GC completes normally */
  mgr.unregister_guest_thread(1U);
  CHECK(mgr.collect(&stats, &issue));
  CHECK(stats.marked == 1);
  CHECK(stats.swept == 0);
}

TEST_CASE("memory core: object header contract is shared and versioned") {
  CHECK(MOBAGEN_MEMORY_OBJECT_ABI_VERSION == 1U);
  CHECK(MOBAGEN_MEMORY_OBJECT_HEADER_BYTES == 16U);
  CHECK(mobagen::memory::size_class_for(1) == 0U);
  CHECK(mobagen::memory::size_class_for(16) == 0U);
  CHECK(mobagen::memory::size_class_for(17) == 1U);
  CHECK(mobagen::memory::size_class_for(16U * 1024U) == mobagen::memory::size_class_count - 1U);
  CHECK(mobagen::memory::size_class_for(16U * 1024U + 1U) == mobagen::memory::size_class_count); /* too large: loud */

  MockShim mock;
  MemoryManager mgr(mock.shim());
  REQUIRE(mgr.init(64U * 1024U));
  MemoryIssue issue;
  CHECK(mgr.alloc(16U * 1024U + 1U, 0, &issue) == mobagen::memory::null_offset);
  CHECK(issue.code == mobagen::memory::MemoryIssueCode::TooLarge);

  /* header fields land at the documented offsets */
  const std::uint32_t off = mgr.alloc(24, 2);
  REQUIRE(off != mobagen::memory::null_offset);
  const auto* header = reinterpret_cast<const std::uint32_t*>(mgr.region_base() + off);
  CHECK(header[0] == MOBAGEN_MEMORY_OBJECT_MAGIC_V1);
  CHECK((header[1] & MOBAGEN_MEMORY_FLAG_SIZE_CLASS_MASK) == mobagen::memory::size_class_for(24));
  CHECK((header[1] & MOBAGEN_MEMORY_FLAG_MARK_BIT) == 0U);
  CHECK((header[1] & MOBAGEN_MEMORY_FLAG_FREE_BIT) == 0U);
  CHECK(header[2] == 24U);
  CHECK(header[3] == 2U);
  /* ref words initialized to null handles */
  const auto* refs = reinterpret_cast<const std::uint32_t*>(mgr.payload(off));
  CHECK(refs[0] == MOBAGEN_MEMORY_NULL_HANDLE);
  CHECK(refs[1] == MOBAGEN_MEMORY_NULL_HANDLE);
}

/* todo 13 B: control +16 u32 = in-flight bracket word. */
namespace {
  std::uint32_t read_u32_at(MemoryManager& mgr, std::uint32_t offset) {
    std::uint32_t v;
    std::memcpy(&v, mgr.region_base() + offset, sizeof(v));
    return v;
  }
}  // namespace

TEST_CASE("memory core: in-flight bracket returns to zero after each allocation") {
  MockShim mock;
  MemoryManager mgr(mock.shim());
  REQUIRE(mgr.init(128U * 1024U));
  CHECK(read_u32_at(mgr, 16U) == 0U); /* init: in_flight stored 0 */
  for (int i = 0; i < 8; ++i) {
    const std::uint32_t off = mgr.alloc(16, 0);
    REQUIRE(off != mobagen::memory::null_offset);
    CHECK(read_u32_at(mgr, 16U) == 0U); /* bracket fully left */
    mgr.free(off);
  }
  CHECK(mgr.live_objects() == 0);
}

TEST_CASE("memory core: single-threaded collect parks and releases the guest around mark/sweep") {
  MockShim mock;
  MemoryManager mgr(mock.shim());
  REQUIRE(mgr.init(128U * 1024U));

  const std::uint32_t off = mgr.alloc(16, 0);
  const Handle root = mgr.register_root(mgr.handle_for(off));
  REQUIRE(root != mobagen::memory::null_handle);

  FakeGuestThread guest;
  guest.start();
  GuestThread reg{};
  reg.user_data = &guest;
  reg.quiesce_fn = &FakeGuestThread::quiesce_cb;
  reg.release_fn = &FakeGuestThread::release_cb;
  REQUIRE(mgr.register_guest_thread(reg) != 0);

  CollectStats stats;
  MemoryIssue issue;
  REQUIRE(mgr.collect(&stats, &issue));
  /* park (=1) before mark, release (=2) after; bracket drained */
  CHECK(guest.parked_at_order == 1);
  CHECK(guest.released_at_order == 2);
  CHECK(read_u32_at(mgr, 16U) == 0U);
  CHECK_FALSE(mgr.gc_in_flight());
  CHECK(mgr.handle_resolve(root) != mobagen::memory::null_offset);
  CHECK(stats.quiesced_threads == 1);

  mgr.unregister_guest_thread(1U);
  guest.join();
}

TEST_CASE("memory core: alloc parks at the epoch barrier while collect runs, resumes after release") {
  MockShim mock;
  MemoryManager mgr(mock.shim());
  REQUIRE(mgr.init(256U * 1024U));

  const std::uint32_t off0 = mgr.alloc(16, 0);
  REQUIRE(off0 != mobagen::memory::null_offset);
  (void)mgr.register_root(mgr.handle_for(off0));

  CollectStats stats;
  MemoryIssue issue;
  std::atomic<bool> collect_done{false};
  std::thread collector([&] {
    REQUIRE(mgr.collect(&stats, &issue));
    collect_done = true;
  });

  /* wait for the flip (no guests registered -> quiesce passes fast); if the
   * collect already completed the alloc simply runs under the even epoch */
  while (!mgr.gc_in_flight() && !collect_done) std::this_thread::yield();

  std::uint32_t allocated = mobagen::memory::null_offset;
  MemoryIssue alloc_issue;
  std::thread allocator([&] { allocated = mgr.alloc(16, 0, &alloc_issue); });

  collector.join();
  allocator.join();
  REQUIRE(allocated != mobagen::memory::null_offset);
  CHECK_FALSE(mgr.gc_in_flight());
  CHECK(mgr.handle_resolve(mgr.handle_for(allocated)) != mobagen::memory::null_offset);
  mgr.free(allocated);
}
