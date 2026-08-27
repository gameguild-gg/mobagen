#include <doctest/doctest.h>
#include "reactive/reactive.hpp"

TEST_CASE("Signal: set triggers computed recompute") {
  reactive::Signal<int> s(1);
  reactive::Computed<int> c([&]() { return s.get() * 2; });
  int observed = 0;
  reactive::Effect e([&]() { observed = c.get(); });
  CHECK(observed == 2);
  s.set(5);
  CHECK(observed == 10);
}

TEST_CASE("Computed: reads cached value between changes") {
  reactive::Signal<int> s(3);
  int compute_count = 0;
  reactive::Computed<int> c([&]() {
    ++compute_count;
    return s.get() * 2;
  });
  CHECK(c.get() == 6);
  CHECK(c.get() == 6);
  CHECK(compute_count == 2);
  s.set(4);
  CHECK(c.get() == 8);
  CHECK(compute_count == 3);
}

TEST_CASE("Effect: re-runs exactly once per dependency change") {
  reactive::Signal<int> s(1);
  int run_count = 0;
  reactive::Effect e([&]() {
    ++run_count;
    (void)s.get();
  });
  CHECK(run_count == 1);
  s.set(2);
  CHECK(run_count == 2);
  s.set(2);
  CHECK(run_count == 2);
  s.set(3);
  CHECK(run_count == 3);
}
