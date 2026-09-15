#include <mobagen/plugin/window_surface_v1.h>

#include <SDL3/SDL.h>
#if defined(SDL_PLATFORM_APPLE)
#  include <SDL3/SDL_metal.h>
#endif

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

  constexpr std::size_t max_title_bytes = 1024;
  constexpr std::size_t max_windows = 64;

  struct WindowSlot {
    SDL_Window* window{};
    std::uint32_t generation{1};
#if defined(SDL_PLATFORM_APPLE)
    SDL_MetalView metal_view{};
#endif
  };

  struct WindowPluginState {
    MobagenHostApiV1 host{};
    std::thread::id owner_thread;
    std::vector<WindowSlot> windows;
    bool owns_video{};
    MobagenWindowSurfaceV1 api{};
  };

  bool on_owner_thread(const WindowPluginState& state) noexcept {
    return state.owner_thread == std::this_thread::get_id();
  }

  WindowSlot* resolve(
      WindowPluginState& state, MobagenWindowHandleV1 handle
  ) noexcept {
    if (handle.index >= state.windows.size()) return nullptr;
    auto& slot = state.windows[handle.index];
    return slot.window != nullptr && slot.generation == handle.generation ? &slot
                                                                          : nullptr;
  }

  MobagenWindowHandleV1 handle_for(
      const WindowPluginState& state, SDL_Window* window
  ) noexcept {
    for (std::size_t index = 0; index < state.windows.size(); ++index) {
      if (state.windows[index].window == window) {
        return {
            static_cast<std::uint32_t>(index), state.windows[index].generation
        };
      }
    }
    return {std::numeric_limits<std::uint32_t>::max(), 0};
  }

  bool ensure_video(WindowPluginState& state) noexcept {
    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0) return true;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return false;
    state.owns_video = true;
    return true;
  }

  void destroy_slot(WindowSlot& slot) noexcept {
#if defined(SDL_PLATFORM_APPLE)
    if (slot.metal_view != nullptr) {
      SDL_Metal_DestroyView(slot.metal_view);
      slot.metal_view = nullptr;
    }
#endif
    if (slot.window != nullptr) SDL_DestroyWindow(slot.window);
    slot.window = nullptr;
    ++slot.generation;
    if (slot.generation == 0) slot.generation = 1;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL create_window(
      void* opaque, const MobagenWindowDescV1* descriptor,
      MobagenWindowHandleV1* handle
  ) noexcept {
    auto* state = static_cast<WindowPluginState*>(opaque);
    if (state == nullptr || descriptor == nullptr || handle == nullptr
        || descriptor->struct_size < MOBAGEN_WINDOW_DESC_V1_SIZE
        || descriptor->width <= 0 || descriptor->height <= 0
        || descriptor->title.size > max_title_bytes
        || (descriptor->title.size != 0 && descriptor->title.data == nullptr)
        || !on_owner_thread(*state)) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    try {
      const auto title = std::string_view{
          descriptor->title.data == nullptr ? "" : descriptor->title.data,
          descriptor->title.size
      };
      if (title.find('\0') != std::string_view::npos || !ensure_video(*state)) {
        return MOBAGEN_STATUS_FAILED;
      }
      SDL_WindowFlags flags = 0;
      if ((descriptor->flags & MOBAGEN_WINDOW_RESIZABLE_V1) != 0)
        flags |= SDL_WINDOW_RESIZABLE;
      if ((descriptor->flags & MOBAGEN_WINDOW_HIGH_PIXEL_DENSITY_V1) != 0)
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
      if ((descriptor->flags & MOBAGEN_WINDOW_HIDDEN_V1) != 0)
        flags |= SDL_WINDOW_HIDDEN;
      const auto owned_title = std::string{title};
      auto* window = SDL_CreateWindow(
          owned_title.c_str(), descriptor->width, descriptor->height, flags
      );
      if (window == nullptr) return MOBAGEN_STATUS_FAILED;

      for (std::size_t index = 0; index < state->windows.size(); ++index) {
        auto& slot = state->windows[index];
        if (slot.window == nullptr) {
          slot.window = window;
          *handle = {static_cast<std::uint32_t>(index), slot.generation};
          return MOBAGEN_STATUS_OK;
        }
      }
      if (state->windows.size() == max_windows) {
        SDL_DestroyWindow(window);
        return MOBAGEN_STATUS_OUT_OF_MEMORY;
      }
      state->windows.push_back({.window = window});
      *handle = {
          static_cast<std::uint32_t>(state->windows.size() - 1),
          state->windows.back().generation
      };
      return MOBAGEN_STATUS_OK;
    } catch (const std::bad_alloc&) {
      return MOBAGEN_STATUS_OUT_OF_MEMORY;
    } catch (...) {
      return MOBAGEN_STATUS_FAILED;
    }
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL destroy_window(
      void* opaque, MobagenWindowHandleV1 handle
  ) noexcept {
    auto* state = static_cast<WindowPluginState*>(opaque);
    if (state == nullptr || !on_owner_thread(*state))
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    auto* slot = resolve(*state, handle);
    if (slot == nullptr) return MOBAGEN_STATUS_NOT_FOUND;
    destroy_slot(*slot);
    return MOBAGEN_STATUS_OK;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL native_surface(
      void* opaque, MobagenWindowHandleV1 handle,
      MobagenNativeSurfaceV1* surface
  ) noexcept {
    auto* state = static_cast<WindowPluginState*>(opaque);
    if (state == nullptr || surface == nullptr
        || surface->struct_size < MOBAGEN_NATIVE_SURFACE_V1_SIZE
        || !on_owner_thread(*state)) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    auto* slot = resolve(*state, handle);
    if (slot == nullptr) return MOBAGEN_STATUS_NOT_FOUND;
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(slot->window, &width, &height) || width <= 0
        || height <= 0) {
      return MOBAGEN_STATUS_FAILED;
    }
    MobagenNativeSurfaceV1 value{
        .struct_size = MOBAGEN_NATIVE_SURFACE_V1_SIZE,
        .width = width,
        .height = height,
    };
#if defined(SDL_PLATFORM_WIN32)
    const auto properties = SDL_GetWindowProperties(slot->window);
    value.kind = MOBAGEN_NATIVE_SURFACE_WIN32_V1;
    value.display = SDL_GetPointerProperty(
        properties, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr
    );
    value.window = SDL_GetPointerProperty(
        properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr
    );
#elif defined(SDL_PLATFORM_APPLE)
    if (slot->metal_view == nullptr)
      slot->metal_view = SDL_Metal_CreateView(slot->window);
    value.kind = MOBAGEN_NATIVE_SURFACE_METAL_LAYER_V1;
    value.window = slot->metal_view == nullptr
                       ? nullptr
                       : SDL_Metal_GetLayer(slot->metal_view);
#elif defined(SDL_PLATFORM_LINUX)
    const auto properties = SDL_GetWindowProperties(slot->window);
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
      value.kind = MOBAGEN_NATIVE_SURFACE_WAYLAND_V1;
      value.display = SDL_GetPointerProperty(
          properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr
      );
      value.window = SDL_GetPointerProperty(
          properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr
      );
    } else {
      value.kind = MOBAGEN_NATIVE_SURFACE_XLIB_V1;
      value.display = SDL_GetPointerProperty(
          properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr
      );
      value.window_id = static_cast<std::uint64_t>(SDL_GetNumberProperty(
          properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0
      ));
    }
#else
    return MOBAGEN_STATUS_UNSUPPORTED;
#endif
    if (value.window == nullptr
        && value.kind != MOBAGEN_NATIVE_SURFACE_XLIB_V1) {
      return MOBAGEN_STATUS_FAILED;
    }
    if (value.kind == MOBAGEN_NATIVE_SURFACE_XLIB_V1
        && (value.display == nullptr || value.window_id == 0)) {
      return MOBAGEN_STATUS_FAILED;
    }
    *surface = value;
    return MOBAGEN_STATUS_OK;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL poll_event(
      void* opaque, MobagenWindowEventV1* output
  ) noexcept {
    auto* state = static_cast<WindowPluginState*>(opaque);
    if (state == nullptr || output == nullptr
        || output->struct_size < MOBAGEN_WINDOW_EVENT_V1_SIZE
        || !on_owner_thread(*state)) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
      MobagenWindowEventV1 translated{
          .struct_size = MOBAGEN_WINDOW_EVENT_V1_SIZE,
          .window = {std::numeric_limits<std::uint32_t>::max(), 0},
      };
      if (event.type == SDL_EVENT_QUIT) {
        translated.type = MOBAGEN_WINDOW_EVENT_QUIT_V1;
      } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        translated.type = MOBAGEN_WINDOW_EVENT_CLOSE_REQUESTED_V1;
        translated.window = handle_for(
            *state, SDL_GetWindowFromID(event.window.windowID)
        );
      } else if (event.type == SDL_EVENT_WINDOW_RESIZED
                 || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        translated.type = MOBAGEN_WINDOW_EVENT_RESIZED_V1;
        translated.window = handle_for(
            *state, SDL_GetWindowFromID(event.window.windowID)
        );
        translated.data1 = event.window.data1;
        translated.data2 = event.window.data2;
      } else {
        continue;
      }
      *output = translated;
      return MOBAGEN_STATUS_OK;
    }
    return MOBAGEN_STATUS_NOT_FOUND;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL configure(
      void* opaque, const MobagenHostApiV1* host, MobagenByteView configuration
  ) noexcept {
    auto* state = static_cast<WindowPluginState*>(opaque);
    if (state == nullptr || host == nullptr || host->publish_capability == nullptr
        || configuration.size != 0) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    state->api = {
        .header = {
            MOBAGEN_WINDOW_SURFACE_V1_SIZE,
            MOBAGEN_WINDOW_SURFACE_V1_ABI_VERSION,
        },
        .window_state = state,
        .create = create_window,
        .destroy = destroy_window,
        .native_surface = native_surface,
        .poll_event = poll_event,
    };
    return host->publish_capability(
        host->host_context,
        {MOBAGEN_WINDOW_SURFACE_V1_ID,
         sizeof(MOBAGEN_WINDOW_SURFACE_V1_ID) - 1},
        MOBAGEN_WINDOW_SURFACE_V1_ABI_VERSION, &state->api,
        MOBAGEN_WINDOW_SURFACE_V1_SIZE
    );
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL start(void* opaque) noexcept {
    return opaque == nullptr ? MOBAGEN_STATUS_INVALID_ARGUMENT : MOBAGEN_STATUS_OK;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL quiesce(void* opaque) noexcept {
    return opaque == nullptr ? MOBAGEN_STATUS_INVALID_ARGUMENT : MOBAGEN_STATUS_OK;
  }

  void MOBAGEN_PLUGIN_CALL stop(void* opaque) noexcept {
    auto* state = static_cast<WindowPluginState*>(opaque);
    if (state == nullptr || !on_owner_thread(*state)) return;
    for (auto& slot : state->windows) destroy_slot(slot);
    state->windows.clear();
    if (state->owns_video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    state->owns_video = false;
  }

  void MOBAGEN_PLUGIN_CALL destroy(void* opaque) noexcept {
    auto* state = static_cast<WindowPluginState*>(opaque);
    if (state == nullptr) return;
    const auto host = state->host;
    state->~WindowPluginState();
    host.deallocate(
        host.host_context, state, sizeof(WindowPluginState),
        alignof(WindowPluginState)
    );
  }

}  // namespace

MOBAGEN_PLUGIN_EXPORT MobagenStatus MOBAGEN_PLUGIN_CALL mobagen_plugin_entry_v1(
    const MobagenHostApiV1* host, MobagenPluginDescriptorV1* descriptor
) {
  static const MobagenStringView provides[] = {
      {MOBAGEN_WINDOW_SURFACE_V1_ID,
       sizeof(MOBAGEN_WINDOW_SURFACE_V1_ID) - 1}
  };
  static const MobagenStringView permissions[] = {
      {"windowing", sizeof("windowing") - 1}
  };
  if (host == nullptr || descriptor == nullptr
      || host->abi_version != MOBAGEN_PLUGIN_ABI_VERSION
      || host->struct_size < MOBAGEN_PLUGIN_HOST_API_V1_SIZE
      || host->allocate == nullptr || host->deallocate == nullptr
      || descriptor->struct_size < MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE) {
    return MOBAGEN_STATUS_UNSUPPORTED;
  }
  auto* memory = host->allocate(
      host->host_context, sizeof(WindowPluginState), alignof(WindowPluginState)
  );
  if (memory == nullptr) return MOBAGEN_STATUS_OUT_OF_MEMORY;
  auto* state = new (memory) WindowPluginState{
      .host = *host,
      .owner_thread = std::this_thread::get_id(),
  };
  *descriptor = {
      .struct_size = MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE,
      .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
      .id = {"mobagen.window.sdl3", sizeof("mobagen.window.sdl3") - 1},
      .version_major = 1,
      .version_minor = 0,
      .version_patch = 0,
      .reload_policy = MOBAGEN_RELOAD_RESTART,
      .plugin_state = state,
      .provides = provides,
      .provides_count = 1,
      .lifecycle = {
          .struct_size = MOBAGEN_PLUGIN_LIFECYCLE_V1_SIZE,
          .configure = configure,
          .start = start,
          .quiesce = quiesce,
          .stop = stop,
          .destroy = destroy,
      },
      .permissions = permissions,
      .permissions_count = 1,
  };
  return MOBAGEN_STATUS_OK;
}
