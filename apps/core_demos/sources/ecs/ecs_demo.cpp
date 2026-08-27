// Demonstrates the ECS storage's three properties: contiguous layout, stable
// pointers across growth, and packing after removal. Uses a tiny 4-per-chunk
// Storage so chunk boundaries are visible with only a handful of elements.
//
//   make core-examples
//   build/native/bin/Release/ecs_demo.exe

#include "sparse_set.hpp"
#include "storage.hpp"

#include <cstdio>

struct Position {
  float x, y, z;
};

int main() {
  ecs::Storage<Position, 4> s;  // 4 components per chunk, to expose boundaries

  std::printf("== ECS storage: data structure / memory / layout ==\n");
  std::printf("sizeof(Position)=%zu, chunk holds %zu components\n\n", sizeof(Position), s.chunk_elems);

  for (std::uint32_t id = 0; id < 6; ++id) s.emplace(id, Position{static_cast<float>(id), 0.f, 0.f});
  std::printf("inserted ids 0..5 -> size=%zu chunks=%zu (expect 2: [0..3][4..5])\n", s.size(), s.chunk_count());

  // (1) Contiguous layout within a chunk: addresses step by sizeof(Position).
  std::printf("\n(1) contiguous within chunk 0:\n");
  for (std::uint32_t id = 0; id < 4; ++id) std::printf("    id %u  &component = %p\n", id, static_cast<void*>(&s.get(id)));

  // (2) Stable pointers across growth (a flat vector would realloc + move these).
  Position* before = &s.get(0);
  for (std::uint32_t id = 6; id < 20; ++id) s.emplace(id, Position{static_cast<float>(id), 0.f, 0.f});
  std::printf("\n(2) grew to size=%zu chunks=%zu -> &component(id 0) %s\n", s.size(), s.chunk_count(),
              before == &s.get(0) ? "UNCHANGED (stable)" : "MOVED (bad!)");

  // (3) Swap-with-last keeps the dense array packed.
  const std::uint32_t last_id = s.ids()[s.size() - 1];
  std::printf("\n(3) remove id 2 (last id is %u) -> last backfills the hole:\n", last_id);
  s.remove(2);
  std::printf("    contains(2)=%d  size=%zu  contains(%u)=%d (still present, packed)\n", s.contains(2), s.size(), last_id, s.contains(last_id));

  // Show it still iterates as a tight packed run.
  std::printf("    packed ids now: ");
  s.each([](std::uint32_t id, Position&) { std::printf("%u ", id); });
  std::printf("\n");
  return 0;
}
