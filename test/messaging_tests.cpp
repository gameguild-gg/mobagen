#include <doctest/doctest.h>
#include "messaging/notifier.hpp"
#include "messaging/event_bus.hpp"

TEST_CASE("Notifier: connect, emit fires listener, disconnect stops it") {
  msg::Notifier<int> notifier;
  int received = 0;
  auto id = notifier.connect([&](int v) { received = v; });
  notifier.emit(42);
  CHECK(received == 42);
  notifier.disconnect(id);
  notifier.emit(99);
  CHECK(received == 42);
}

TEST_CASE("EventBus: post queues event, process drains in order") {
  msg::EventBus bus;
  struct EventA {
    int value;
  };
  std::vector<int> values;
  bus.subscribe<EventA>([&](const EventA& e) { values.push_back(e.value); });
  bus.post(EventA{1});
  bus.post(EventA{2});
  bus.post(EventA{3});
  bus.process();
  REQUIRE(values.size() == 3);
  CHECK(values[0] == 1);
  CHECK(values[1] == 2);
  CHECK(values[2] == 3);
}

TEST_CASE("EventBus: separate channels per type, no cross-fire") {
  msg::EventBus bus;
  struct EventX {
    int x;
  };
  struct EventY {
    int y;
  };
  int x_count = 0, y_count = 0;
  bus.subscribe<EventX>([&](const EventX&) { ++x_count; });
  bus.subscribe<EventY>([&](const EventY&) { ++y_count; });
  bus.post(EventX{1});
  bus.post(EventY{2});
  bus.process();
  CHECK(x_count == 1);
  CHECK(y_count == 1);
}
