#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "app/app.hpp"
#include "graphics/composition.hpp"

namespace graphics_allocation_probe {
  std::atomic_bool enabled{false};
  std::atomic_size_t count{0};
}  // namespace graphics_allocation_probe

void* operator new(std::size_t size) {
  if (graphics_allocation_probe::enabled.load(std::memory_order_relaxed)) {
    graphics_allocation_probe::count.fetch_add(1, std::memory_order_relaxed);
  }
  if (void* allocation = std::malloc(size == 0 ? 1 : size); allocation != nullptr) return allocation;
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void* allocation) noexcept { std::free(allocation); }

void operator delete[](void* allocation) noexcept { ::operator delete(allocation); }

void operator delete(void* allocation, std::size_t) noexcept { std::free(allocation); }

void operator delete[](void* allocation, std::size_t) noexcept { std::free(allocation); }

#ifndef __EMSCRIPTEN__
TEST_CASE("Module composition: SDL3 and WebGPU providers run through the app host") {
  mobagen::compositions::GraphicsAppCallbacks callbacks;
  app::App application;
  application.callbacks = &callbacks;
  application.settings.width = 64;
  application.settings.height = 64;

  char arg0[] = "GraphicsCompositionTests";
  char null_gpu_flag[] = "--mobagen-null-gpu";
  char exit_after_frame_flag[] = "--mobagen-exit-after-frame";
  char* argv[] = {arg0, null_gpu_flag, exit_after_frame_flag, nullptr};

  REQUIRE(app::host_init(application, 3, argv) == SDL_APP_CONTINUE);
  REQUIRE(application.settings.render_mode == app::AppSettings::RenderMode::HeadlessNull);
  REQUIRE(application.device() != nullptr);
  CHECK(application.window == nullptr);
  CHECK(callbacks.active());
  CHECK_FALSE(callbacks.stopped());

  const auto* registry = callbacks.registry();
  const auto* resolution = callbacks.resolution();
  REQUIRE(registry != nullptr);
  REQUIRE(resolution != nullptr);
  REQUIRE(resolution->lifecycle_order().size() == 4);

  const std::array expected_providers{
      std::string{"mobagen.platform.sdl3"},
      std::string{"mobagen.target.sdl3"},
      std::string{"mobagen.render.webgpu"},
      std::string{"mobagen.runtime.graphics"},
  };
  for (std::size_t index = 0; index < expected_providers.size(); ++index) {
    const auto* provider = registry->provider(resolution->lifecycle_order()[index]);
    REQUIRE(provider != nullptr);
    CHECK(provider->id == expected_providers[index]);
  }

  CHECK(app::host_iterate(application) == SDL_APP_CONTINUE);
  CHECK(callbacks.ticks() == 1);
  CHECK(callbacks.draws() == 1);
  CHECK(app::host_iterate(application) == SDL_APP_SUCCESS);
  CHECK(callbacks.ticks() == 1);
  CHECK(callbacks.draws() == 1);

  app::host_quit(application);
  CHECK_FALSE(callbacks.active());
  CHECK(callbacks.stopped());
  CHECK(callbacks.resources_alive_during_shutdown());
  CHECK(application.device() == nullptr);

  const std::vector<std::string> expected_trace{
      "configure:mobagen.platform.sdl3",  "configure:mobagen.target.sdl3", "configure:mobagen.render.webgpu", "configure:mobagen.runtime.graphics",
      "start:mobagen.platform.sdl3",      "start:mobagen.target.sdl3",     "start:mobagen.render.webgpu",     "start:mobagen.runtime.graphics",
      "quiesce:mobagen.runtime.graphics", "quiesce:mobagen.render.webgpu", "quiesce:mobagen.target.sdl3",     "quiesce:mobagen.platform.sdl3",
      "stop:mobagen.runtime.graphics",    "stop:mobagen.render.webgpu",    "stop:mobagen.target.sdl3",        "stop:mobagen.platform.sdl3",
  };
  CHECK(callbacks.lifecycle_trace() == expected_trace);
}

TEST_CASE("Module composition: warmed graphics tick dispatch performs zero allocations") {
  app::AppCallbacks callbacks;
  app::App application;
  application.callbacks = &callbacks;
  application.settings.render_mode = app::AppSettings::RenderMode::HeadlessNull;
  application.settings.width = 16;
  application.settings.height = 16;

  char arg0[] = "GraphicsCompositionTests";
  char* argv[] = {arg0, nullptr};
  REQUIRE(app::host_init(application, 1, argv) == SDL_APP_CONTINUE);

#  ifdef _WIN32
  constexpr auto target = mobagen::modules::TargetPlatform::Windows;
#  elif defined(__APPLE__)
  constexpr auto target = mobagen::modules::TargetPlatform::MacOS;
#  else
  constexpr auto target = mobagen::modules::TargetPlatform::Linux;
#  endif
  auto result = mobagen::compositions::create_graphics_composition(application, mobagen::compositions::default_graphics_product(), target, "release");
  REQUIRE(result.ok());
  REQUIRE(result.composition->tick(0.0f));

  graphics_allocation_probe::count.store(0, std::memory_order_relaxed);
  graphics_allocation_probe::enabled.store(true, std::memory_order_relaxed);
  bool all_ticks_succeeded = true;
  for (std::size_t invocation = 0; invocation < 100'000; ++invocation) all_ticks_succeeded &= result.composition->tick(0.0f);
  graphics_allocation_probe::enabled.store(false, std::memory_order_relaxed);

  CHECK(all_ticks_succeeded);
  CHECK(graphics_allocation_probe::count.load(std::memory_order_relaxed) == 0);
  CHECK(result.composition->ticks() == 100'001);
  CHECK(result.composition->stop().ok());
  app::host_quit(application);
}
#endif

TEST_CASE("Module composition: graphics providers reject a host without WebGPU") {
  mobagen::compositions::GraphicsAppCallbacks callbacks;
  app::App application;
  application.callbacks = &callbacks;

  char arg0[] = "GraphicsCompositionTests";
  char headless_flag[] = "--mobagen-headless";
  char* argv[] = {arg0, headless_flag, nullptr};

  CHECK(app::host_init(application, 2, argv) == SDL_APP_FAILURE);
  CHECK_FALSE(callbacks.active());
  REQUIRE(callbacks.issues().size() == 1);
  CHECK(callbacks.issues().front().code == mobagen::compositions::GraphicsCompositionIssueCode::HostResources);
  app::host_quit(application);
  CHECK_FALSE(callbacks.stopped());
}
