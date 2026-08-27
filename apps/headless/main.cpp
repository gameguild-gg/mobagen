#include "ecs/world.hpp"
#include "jobs/scheduler.hpp"
#include "scene/transform.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>

#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#endif

// Simulation state component (replaces the OOP HeadlessTestObject fields).
struct SimState {
  float totalTime = 0.0f;
  float maxRunTime = 5.0f;  // Run for 5 seconds
  int frameCount = 0;
};

int main() {
  std::printf("Creating Headless World\n");

  ecs::World world;
  jobs::Scheduler sched;

  // Create the simulation entity with state + a root Transform.
  ecs::Entity simEnt = world.create();
  world.add<SimState>(simEnt);
  world.add<scene::Transform>(simEnt);

  std::printf("Headless World Created\n");
  std::printf("Starting Headless Simulation\n");

  constexpr float kTargetDt = 1.0f / 60.0f;  // 60 FPS simulation step
  bool running = true;

  while (running) {
#ifdef __EMSCRIPTEN__
    emscripten_sleep(1);  // yield to the browser event loop via asyncify (prevents busy spin)
#endif
    // Update system: advance time, log, check stop condition.
    world.view<SimState>([&](ecs::Entity, SimState& s) {
      s.totalTime += kTargetDt;
      s.frameCount++;

      // Log progress every second (every 60 frames at 60 FPS).
      if (s.frameCount % 60 == 0) {
        std::printf("Headless simulation running: %.2f seconds, frame %d\n", s.totalTime, s.frameCount);
      }

      // Exit after maxRunTime seconds.
      if (s.totalTime >= s.maxRunTime) {
        std::printf("Headless simulation completed after %.2f seconds (%d frames)\n", s.totalTime, s.frameCount);
        running = false;
      }
    });
  }

  sched.shutdown();
  std::printf("Headless Simulation Stopped\n");

  std::cout << "Headless simulation completed successfully!" << std::endl;
  return 0;
}
