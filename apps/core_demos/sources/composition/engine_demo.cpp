// engine_demo — the whole orchestration backbone composing into one "tick":
//   reactive (params)  +  EventBus (events)  +  ECS World (entities)
//   +  Scheduler (parallel system)  +  scene TransformSystem (hierarchy).
// This is the "Coordinator" idea from ENGINE_ARCHITECTURE, in miniature — and the
// proof that the independent modules snap together (the app is the glue).

#include "event_bus.hpp"  // core/sources/messaging
#include "reactive.hpp"   // core/sources/reactive
#include "scheduler.hpp"  // core/sources/jobs
#include "transform.hpp"  // core/sources/scene
#include "transform_system.hpp"
#include "world.hpp"  // core/sources/ecs

#include <glm/gtc/quaternion.hpp>
#include <cstdio>

struct Spin {
  float speed;
};  // a sim component
struct VolumeLoaded {
  int entities;
};  // a coarse event

int main() {
  // --- control plane: reactive params (window/level -> LUT) ---
  reactive::Signal<float> window_center{40}, window_width{400};
  reactive::Computed<float> lut_key([&] { return window_center.get() * 1000.f + window_width.get(); });
  int lut_uploads = 0;
  reactive::Effect upload_lut([&] {
    (void)lut_key.get();
    ++lut_uploads;
  });

  // --- the engine's pieces ---
  ecs::World world;
  scene::TransformSystem transforms;
  jobs::Scheduler sched;
  msg::EventBus bus;

  // Loading a volume spawns entities (coarse event -> world mutation).
  bus.subscribe<VolumeLoaded>([&](const VolumeLoaded& e) {
    for (int i = 0; i < e.entities; ++i) {
      ecs::Entity ent = world.create();
      scene::Transform t;
      t.position = {static_cast<float>(i), 0.f, 0.f};
      world.add<scene::Transform>(ent, t);
      world.add<Spin>(ent, Spin{0.2f * static_cast<float>(i % 8 + 1)});  // rad/tick
    }
  });

  // One frame:  drain events -> parallel spin system -> resolve transforms.
  auto tick = [&](float dt) {
    const std::size_t n = world.count<scene::Transform>();
    jobs::WaitGroup wg;  // systems-as-jobs:
    sched.parallel_for(
        n, 4096,
        [&](std::size_t lo, std::size_t hi) {
          world.apply_range<scene::Transform, Spin>(
              lo, hi, [&](ecs::Entity, scene::Transform& t, Spin& s) { t.rotation = glm::angleAxis(s.speed * dt, glm::vec3(0, 1, 0)) * t.rotation; });
        },
        wg);
    sched.wait(wg);
    transforms.update(world);  // scene graph -> world matrices
  };

  std::printf("== engine tick: all modules composed ==\n");
  bus.post(VolumeLoaded{100000});  // queue a "volume load"
  bus.process();                   // drain now -> spawn the entities
  transforms.rebuild(world);       // build the flat transform order once (structure set)
  window_center.set(20);           // reactive param change -> Effect re-fires
  window_width.set(600);

  for (int f = 1; f <= 3; ++f) tick(1.0f);  // dt=1 tick; spin is visible

  std::printf("entities spawned (via EventBus) : %zu\n", world.alive());
  std::printf("LUT uploads (via reactive Effect): %d\n", lut_uploads);

  bool sampled = false;
  world.view<scene::Transform, Spin>([&](ecs::Entity, scene::Transform& t, Spin& s) {
    if (!sampled) {
      sampled = true;
      std::printf("sample entity: spin=%.2f  rotation.w=%.4f (was 1.0 -> spun by the parallel system)\n", s.speed, t.rotation.w);
    }
  });

  sched.shutdown();
  return 0;
}
