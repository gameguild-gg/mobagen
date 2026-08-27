// Test for resource::ResourceRegistry — stable handles, stale-handle safety,
// slot recycling. Built with em++ + run under node while native is unavailable:
//   em++ -std=c++20 apps/core_demos/sources/resource/resource_registry_test.cpp -I core/sources/resource \
//        -o build/resource_test.js && node build/resource_test.js
#include "resource_registry.hpp"

#include <cassert>
#include <cstdio>

struct Asset {
  int id = 0;
  float weight = 0.0f;
};

int main() {
  resource::ResourceRegistry<Asset> reg;
  using Handle = resource::ResourceRegistry<Asset>::Handle;

  Handle a = reg.create(Asset{1, 1.5f});
  Handle b = reg.create(Asset{2, 2.5f});
  assert(reg.size() == 2);
  assert(reg.get(a) && reg.get(a)->id == 1);
  assert(reg.get(b) && reg.get(b)->weight == 2.5f);

  // A default/null handle never resolves.
  assert(!reg.valid(reg.null_handle()));
  assert(reg.get(reg.null_handle()) == nullptr);

  // Release a -> its handle goes stale (use-after-free returns null).
  Handle aStale = a;
  reg.release(a);
  assert(!reg.valid(aStale));
  assert(reg.get(aStale) == nullptr);
  assert(reg.size() == 1);
  assert(reg.get(b) && reg.get(b)->id == 2);  // b unaffected

  // Create again -> recycles a's slot, but the OLD handle stays stale because
  // the generation was bumped.
  Handle c = reg.create(Asset{3, 3.5f});
  assert(reg.valid(c) && reg.get(c)->id == 3);
  assert(c.index == aStale.index && "slot should be recycled");
  assert(c.generation != aStale.generation && "generation must differ");
  assert(!reg.valid(aStale) && "old handle to recycled slot must stay invalid");
  assert(reg.size() == 2);

  std::printf(
      "resource_registry OK: stable handles, stale-handle safety, "
      "slot recycling + generation bump verified\n");
  return 0;
}
