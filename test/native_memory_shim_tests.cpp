#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include "memory/native_shim.hpp"
#include "memory/shim.hpp"

using mobagen::memory::NativeShim;

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
