// Demonstrates the two occurrence-messaging mechanisms:
//   Notifier  — synchronous, per-instance, runs slots inline on emit.
//   EventBus  — asynchronous, by-type, queues events until process() drains them.

#include "event_bus.hpp"
#include "notifier.hpp"

#include <cstdio>

using namespace msg;

struct VolumeLoaded {
  int id;
  int w, h, d;
};
struct SceneChanged {};

int main() {
  // --- Notifier: synchronous, in connection order, per instance ---
  std::printf("== Notifier (sync) ==\n");
  Notifier<int> on_clicked;  // payload = which button
  auto a = on_clicked.connect([](int b) { std::printf("  [slot A] clicked %d\n", b); });
  on_clicked.connect([](int b) { std::printf("  [slot B] clicked %d\n", b); });
  std::printf("emit(7) runs slots NOW:\n");
  on_clicked.emit(7);
  on_clicked.disconnect(a);
  std::printf("after disconnect A, emit(8):\n");
  on_clicked.emit(8);

  // --- EventBus: asynchronous, by type, drained on process() ---
  std::printf("\n== EventBus (async) ==\n");
  EventBus bus;
  bus.subscribe<VolumeLoaded>([](const VolumeLoaded& e) { std::printf("  [handler] VolumeLoaded id=%d %dx%dx%d\n", e.id, e.w, e.h, e.d); });
  bus.subscribe<SceneChanged>([](const SceneChanged&) { std::printf("  [handler] SceneChanged\n"); });

  std::printf("post VolumeLoaded + SceneChanged (queued, NOT handled yet)\n");
  bus.post(VolumeLoaded{1, 96, 96, 96});
  bus.post(SceneChanged{});
  std::printf("bus.process() -> handlers run now:\n");
  bus.process();
  return 0;
}
