#include <doctest/doctest.h>

#include "plugins/plugin_activation.hpp"

#include <mobagen/plugin/render_backend_v1.h>
#include <mobagen/plugin/window_surface_v1.h>

#include <limits>
#include <string>
#include <vector>

TEST_CASE("Runtime plugin adapters: SDL3 and WebGPU publish lazy native capabilities") {
  using namespace mobagen;
  plugins::PluginHost host;

  auto loaded_window = plugins::load_native_plugin_binary(MOBAGEN_WINDOW_SDL3_PLUGIN_PATH, host.api());
  REQUIRE(loaded_window.plugin.has_value());
  CHECK(loaded_window.plugin->contract().provider.id == "mobagen.window.sdl3");
  CHECK(loaded_window.plugin->contract().provider.provides == std::vector<std::string>{MOBAGEN_WINDOW_SURFACE_V1_ID});
  CHECK(loaded_window.plugin->contract().provider.permissions == std::vector<std::string>{"windowing"});

  auto window = plugins::activate_loaded_native_plugin(std::move(*loaded_window.plugin), host);
  REQUIRE(window.ok());
  const auto windows = host.find<MobagenWindowSurfaceV1>(MOBAGEN_WINDOW_SURFACE_V1_ID, MOBAGEN_WINDOW_SURFACE_V1_ABI_VERSION);
  REQUIRE(windows.has_value());
  CHECK((*windows)->header.struct_size == MOBAGEN_WINDOW_SURFACE_V1_SIZE);
  CHECK((*windows)->create != nullptr);
  CHECK((*windows)->native_surface != nullptr);
  MobagenNativeSurfaceV1 missing_surface{.struct_size = MOBAGEN_NATIVE_SURFACE_V1_SIZE};
  CHECK((*windows)->native_surface((*windows)->window_state, {std::numeric_limits<std::uint32_t>::max(), 1}, &missing_surface)
        == MOBAGEN_STATUS_NOT_FOUND);

  auto loaded_render = plugins::load_native_plugin_binary(MOBAGEN_RENDER_WEBGPU_PLUGIN_PATH, host.api());
  REQUIRE(loaded_render.plugin.has_value());
  CHECK(loaded_render.plugin->contract().provider.id == "mobagen.render.webgpu");
  CHECK(loaded_render.plugin->contract().provider.provides == std::vector<std::string>{MOBAGEN_RENDER_BACKEND_V1_ID});
  CHECK(loaded_render.plugin->contract().provider.required == std::vector<std::string>{MOBAGEN_WINDOW_SURFACE_V1_ID});
  CHECK(loaded_render.plugin->contract().provider.permissions == std::vector<std::string>{"gpu"});

  auto render = plugins::activate_loaded_native_plugin(std::move(*loaded_render.plugin), host);
  REQUIRE(render.ok());
  const auto renderer = host.find<MobagenRenderBackendV1>(MOBAGEN_RENDER_BACKEND_V1_ID, MOBAGEN_RENDER_BACKEND_V1_ABI_VERSION);
  REQUIRE(renderer.has_value());
  CHECK((*renderer)->header.struct_size == MOBAGEN_RENDER_BACKEND_V1_SIZE);
  CHECK((*renderer)->create_context != nullptr);
  CHECK((*renderer)->tick != nullptr);
  MobagenRenderBackendHandlesV1 handles{.struct_size = MOBAGEN_RENDER_BACKEND_HANDLES_V1_SIZE};
  CHECK((*renderer)->get_handles((*renderer)->render_state, {std::numeric_limits<std::uint32_t>::max(), 1}, &handles) == MOBAGEN_STATUS_NOT_FOUND);

  MobagenRenderContextDescV1 headless{
      .struct_size = MOBAGEN_RENDER_CONTEXT_DESC_V1_SIZE,
      .power_preference = MOBAGEN_RENDER_POWER_DEFAULT_V1,
      .backend_type = MOBAGEN_RENDER_BACKEND_NULL_V1,
      .want_surface = 0,
  };
  MobagenRenderContextHandleV1 context{};
  REQUIRE((*renderer)->create_context((*renderer)->render_state, &headless, &context) == MOBAGEN_STATUS_OK);
  handles = {.struct_size = MOBAGEN_RENDER_BACKEND_HANDLES_V1_SIZE};
  REQUIRE((*renderer)->get_handles((*renderer)->render_state, context, &handles) == MOBAGEN_STATUS_OK);
  CHECK(handles.device != nullptr);
  CHECK(handles.queue != nullptr);
  CHECK(handles.surface == nullptr);
  CHECK((*renderer)->tick((*renderer)->render_state, context) == MOBAGEN_STATUS_OK);
  CHECK((*renderer)->destroy_context((*renderer)->render_state, context) == MOBAGEN_STATUS_OK);
  CHECK((*renderer)->get_handles((*renderer)->render_state, context, &handles) == MOBAGEN_STATUS_NOT_FOUND);

  REQUIRE(render.activation->quiesce().ok());
  REQUIRE(render.activation->stop().ok());
  REQUIRE(window.activation->quiesce().ok());
  REQUIRE(window.activation->stop().ok());
}

TEST_CASE("Runtime plugin adapters: renderer rejects activation without its window dependency") {
  using namespace mobagen;
  plugins::PluginHost host;
  auto loaded = plugins::load_native_plugin_binary(MOBAGEN_RENDER_WEBGPU_PLUGIN_PATH, host.api());
  REQUIRE(loaded.plugin.has_value());

  const auto render = plugins::activate_loaded_native_plugin(std::move(*loaded.plugin), host);

  CHECK_FALSE(render.ok());
  REQUIRE(render.issues.size() == 1);
  CHECK(render.issues.front().phase == plugins::NativePluginLifecyclePhase::Configure);
  CHECK(render.issues.front().status == MOBAGEN_STATUS_NOT_FOUND);
}
