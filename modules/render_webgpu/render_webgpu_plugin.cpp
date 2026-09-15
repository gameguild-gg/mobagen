#include "app/webgpu_context.hpp"

#include <mobagen/plugin/render_backend_v1.h>

#include <webgpu/webgpu.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace {

  constexpr std::size_t max_contexts = 32;

  struct RenderSlot {
    std::unique_ptr<app::WebGPUContext> context;
    std::uint32_t generation{1};
  };

  struct RenderPluginState {
    MobagenHostApiV1 host{};
    std::thread::id owner_thread;
    const MobagenWindowSurfaceV1* windows{};
    std::vector<RenderSlot> contexts;
    MobagenRenderBackendV1 api{};
  };

  bool on_owner_thread(const RenderPluginState& state) noexcept {
    return state.owner_thread == std::this_thread::get_id();
  }

  RenderSlot* resolve(
      RenderPluginState& state, MobagenRenderContextHandleV1 handle
  ) noexcept {
    if (handle.index >= state.contexts.size()) return nullptr;
    auto& slot = state.contexts[handle.index];
    return slot.context != nullptr && slot.generation == handle.generation ? &slot
                                                                           : nullptr;
  }

  bool map_native_surface(
      const MobagenNativeSurfaceV1& source, app::NativeSurfaceSource& destination
  ) noexcept {
    switch (source.kind) {
      case MOBAGEN_NATIVE_SURFACE_WIN32_V1:
        destination.kind = app::NativeSurfaceKind::Win32;
        break;
      case MOBAGEN_NATIVE_SURFACE_METAL_LAYER_V1:
        destination.kind = app::NativeSurfaceKind::MetalLayer;
        break;
      case MOBAGEN_NATIVE_SURFACE_WAYLAND_V1:
        destination.kind = app::NativeSurfaceKind::Wayland;
        break;
      case MOBAGEN_NATIVE_SURFACE_XLIB_V1:
        destination.kind = app::NativeSurfaceKind::Xlib;
        break;
      default:
        return false;
    }
    destination.display = source.display;
    destination.window = source.window;
    destination.window_id = source.window_id;
    destination.width = source.width;
    destination.height = source.height;
    return true;
  }

  bool map_context_descriptor(
      RenderPluginState& state, const MobagenRenderContextDescV1& source,
      app::ContextDesc& destination, app::NativeSurfaceSource& native
  ) noexcept {
    switch (source.power_preference) {
      case MOBAGEN_RENDER_POWER_DEFAULT_V1:
        destination.power_preference = WGPUPowerPreference_Undefined;
        break;
      case MOBAGEN_RENDER_POWER_LOW_V1:
        destination.power_preference = WGPUPowerPreference_LowPower;
        break;
      case MOBAGEN_RENDER_POWER_HIGH_V1:
        destination.power_preference = WGPUPowerPreference_HighPerformance;
        break;
      default:
        return false;
    }
    switch (source.backend_type) {
      case MOBAGEN_RENDER_BACKEND_DEFAULT_V1:
        destination.backend_type = WGPUBackendType_Undefined;
        break;
      case MOBAGEN_RENDER_BACKEND_NULL_V1:
        destination.backend_type = WGPUBackendType_Null;
        break;
      default:
        return false;
    }
    if (source.want_surface > 1) return false;
    destination.want_surface = source.want_surface != 0;
    if (!destination.want_surface) return true;
    if (state.windows == nullptr || state.windows->native_surface == nullptr)
      return false;
    MobagenNativeSurfaceV1 surface{.struct_size = MOBAGEN_NATIVE_SURFACE_V1_SIZE};
    if (state.windows->native_surface(
            state.windows->window_state, source.window, &surface
        ) != MOBAGEN_STATUS_OK
        || !map_native_surface(surface, native)) {
      return false;
    }
    destination.native_surface = &native;
    return true;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL create_context(
      void* opaque, const MobagenRenderContextDescV1* descriptor,
      MobagenRenderContextHandleV1* handle
  ) noexcept {
    auto* state = static_cast<RenderPluginState*>(opaque);
    if (state == nullptr || descriptor == nullptr || handle == nullptr
        || descriptor->struct_size < MOBAGEN_RENDER_CONTEXT_DESC_V1_SIZE
        || descriptor->power_preference > MOBAGEN_RENDER_POWER_HIGH_V1
        || descriptor->backend_type > MOBAGEN_RENDER_BACKEND_NULL_V1
        || descriptor->want_surface > 1
        || !on_owner_thread(*state)) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    try {
      app::ContextDesc context_descriptor{};
      app::NativeSurfaceSource native_surface{};
      if (!map_context_descriptor(
              *state, *descriptor, context_descriptor, native_surface
          )) {
        return MOBAGEN_STATUS_NOT_FOUND;
      }
      auto context = std::make_unique<app::WebGPUContext>();
      if (!context->init(context_descriptor)) return MOBAGEN_STATUS_FAILED;
      for (std::size_t index = 0; index < state->contexts.size(); ++index) {
        auto& slot = state->contexts[index];
        if (slot.context == nullptr) {
          slot.context = std::move(context);
          *handle = {static_cast<std::uint32_t>(index), slot.generation};
          return MOBAGEN_STATUS_OK;
        }
      }
      if (state->contexts.size() == max_contexts)
        return MOBAGEN_STATUS_OUT_OF_MEMORY;
      state->contexts.push_back({.context = std::move(context)});
      *handle = {
          static_cast<std::uint32_t>(state->contexts.size() - 1),
          state->contexts.back().generation
      };
      return MOBAGEN_STATUS_OK;
    } catch (const std::bad_alloc&) {
      return MOBAGEN_STATUS_OUT_OF_MEMORY;
    } catch (...) {
      return MOBAGEN_STATUS_FAILED;
    }
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL destroy_context(
      void* opaque, MobagenRenderContextHandleV1 handle
  ) noexcept {
    auto* state = static_cast<RenderPluginState*>(opaque);
    if (state == nullptr || !on_owner_thread(*state))
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    auto* slot = resolve(*state, handle);
    if (slot == nullptr) return MOBAGEN_STATUS_NOT_FOUND;
    slot->context.reset();
    ++slot->generation;
    if (slot->generation == 0) slot->generation = 1;
    return MOBAGEN_STATUS_OK;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL get_handles(
      void* opaque, MobagenRenderContextHandleV1 handle,
      MobagenRenderBackendHandlesV1* output
  ) noexcept {
    auto* state = static_cast<RenderPluginState*>(opaque);
    if (state == nullptr || output == nullptr || !on_owner_thread(*state)
        || output->struct_size < MOBAGEN_RENDER_BACKEND_HANDLES_V1_SIZE) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    auto* slot = resolve(*state, handle);
    if (slot == nullptr) return MOBAGEN_STATUS_NOT_FOUND;
    *output = {
        .struct_size = MOBAGEN_RENDER_BACKEND_HANDLES_V1_SIZE,
        .device = slot->context->device(),
        .queue = slot->context->queue(),
        .surface = slot->context->surface(),
        .surface_format =
            static_cast<std::uint32_t>(slot->context->surface_format()),
    };
    return MOBAGEN_STATUS_OK;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL configure_surface(
      void* opaque, MobagenRenderContextHandleV1 handle, std::int32_t width,
      std::int32_t height
  ) noexcept {
    auto* state = static_cast<RenderPluginState*>(opaque);
    if (state == nullptr || !on_owner_thread(*state) || width <= 0 || height <= 0)
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    auto* slot = resolve(*state, handle);
    if (slot == nullptr) return MOBAGEN_STATUS_NOT_FOUND;
    return slot->context->configure_surface(width, height) ? MOBAGEN_STATUS_OK
                                                           : MOBAGEN_STATUS_FAILED;
  }

  MobagenRenderFrameStatusV1 frame_status(
      WGPUSurfaceGetCurrentTextureStatus status
  ) noexcept {
    switch (status) {
      case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
        return MOBAGEN_RENDER_FRAME_OPTIMAL_V1;
      case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
        return MOBAGEN_RENDER_FRAME_SUBOPTIMAL_V1;
      case WGPUSurfaceGetCurrentTextureStatus_Timeout:
        return MOBAGEN_RENDER_FRAME_TIMEOUT_V1;
      case WGPUSurfaceGetCurrentTextureStatus_Outdated:
        return MOBAGEN_RENDER_FRAME_OUTDATED_V1;
      case WGPUSurfaceGetCurrentTextureStatus_Lost:
        return MOBAGEN_RENDER_FRAME_LOST_V1;
      default:
        return MOBAGEN_RENDER_FRAME_ERROR_V1;
    }
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL acquire_frame(
      void* opaque, MobagenRenderContextHandleV1 handle,
      MobagenRenderFrameV1* output
  ) noexcept {
    auto* state = static_cast<RenderPluginState*>(opaque);
    if (state == nullptr || output == nullptr
        || output->struct_size < MOBAGEN_RENDER_FRAME_V1_SIZE
        || !on_owner_thread(*state)) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    auto* slot = resolve(*state, handle);
    if (slot == nullptr) return MOBAGEN_STATUS_NOT_FOUND;
    const auto acquired = slot->context->acquire();
    *output = {
        .struct_size = MOBAGEN_RENDER_FRAME_V1_SIZE,
        .texture = acquired.texture,
        .status = frame_status(acquired.status),
    };
    return MOBAGEN_STATUS_OK;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL release_frame(
      void* opaque, MobagenRenderFrameV1 frame
  ) noexcept {
    auto* state = static_cast<RenderPluginState*>(opaque);
    if (state == nullptr || !on_owner_thread(*state)
        || frame.struct_size < MOBAGEN_RENDER_FRAME_V1_SIZE
        || frame.texture == nullptr) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    wgpuTextureRelease(static_cast<WGPUTexture>(frame.texture));
    return MOBAGEN_STATUS_OK;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL present(
      void* opaque, MobagenRenderContextHandleV1 handle
  ) noexcept {
    auto* state = static_cast<RenderPluginState*>(opaque);
    if (state == nullptr || !on_owner_thread(*state))
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    auto* slot = resolve(*state, handle);
    if (slot == nullptr) return MOBAGEN_STATUS_NOT_FOUND;
    return slot->context->present() ? MOBAGEN_STATUS_OK : MOBAGEN_STATUS_FAILED;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL tick(
      void* opaque, MobagenRenderContextHandleV1 handle
  ) noexcept {
    auto* state = static_cast<RenderPluginState*>(opaque);
    if (state == nullptr || !on_owner_thread(*state))
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    auto* slot = resolve(*state, handle);
    if (slot == nullptr) return MOBAGEN_STATUS_NOT_FOUND;
    return slot->context->tick() ? MOBAGEN_STATUS_OK : MOBAGEN_STATUS_FAILED;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL configure(
      void* opaque, const MobagenHostApiV1* host, MobagenByteView configuration
  ) noexcept {
    auto* state = static_cast<RenderPluginState*>(opaque);
    if (state == nullptr || host == nullptr || host->find_capability == nullptr
        || host->publish_capability == nullptr || configuration.size != 0) {
      return MOBAGEN_STATUS_INVALID_ARGUMENT;
    }
    const void* windows = nullptr;
    std::uint32_t windows_size = 0;
    const auto found = host->find_capability(
        host->host_context,
        {MOBAGEN_WINDOW_SURFACE_V1_ID,
         sizeof(MOBAGEN_WINDOW_SURFACE_V1_ID) - 1},
        MOBAGEN_WINDOW_SURFACE_V1_ABI_VERSION, &windows, &windows_size
    );
    if (found != MOBAGEN_STATUS_OK || windows == nullptr
        || windows_size < MOBAGEN_WINDOW_SURFACE_V1_SIZE) {
      return found == MOBAGEN_STATUS_OK ? MOBAGEN_STATUS_UNSUPPORTED : found;
    }
    const auto* window_api = static_cast<const MobagenWindowSurfaceV1*>(windows);
    if (window_api->header.struct_size < MOBAGEN_WINDOW_SURFACE_V1_SIZE
        || window_api->header.abi_version != MOBAGEN_WINDOW_SURFACE_V1_ABI_VERSION
        || window_api->native_surface == nullptr) {
      return MOBAGEN_STATUS_UNSUPPORTED;
    }
    state->windows = window_api;
    state->api = {
        .header = {
            MOBAGEN_RENDER_BACKEND_V1_SIZE,
            MOBAGEN_RENDER_BACKEND_V1_ABI_VERSION,
        },
        .render_state = state,
        .create_context = create_context,
        .destroy_context = destroy_context,
        .get_handles = get_handles,
        .configure_surface = configure_surface,
        .acquire_frame = acquire_frame,
        .release_frame = release_frame,
        .present = present,
        .tick = tick,
    };
    return host->publish_capability(
        host->host_context,
        {MOBAGEN_RENDER_BACKEND_V1_ID,
         sizeof(MOBAGEN_RENDER_BACKEND_V1_ID) - 1},
        MOBAGEN_RENDER_BACKEND_V1_ABI_VERSION, &state->api,
        MOBAGEN_RENDER_BACKEND_V1_SIZE
    );
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL start(void* opaque) noexcept {
    return opaque == nullptr ? MOBAGEN_STATUS_INVALID_ARGUMENT : MOBAGEN_STATUS_OK;
  }

  MobagenStatus MOBAGEN_PLUGIN_CALL quiesce(void* opaque) noexcept {
    return opaque == nullptr ? MOBAGEN_STATUS_INVALID_ARGUMENT : MOBAGEN_STATUS_OK;
  }

  void MOBAGEN_PLUGIN_CALL stop(void* opaque) noexcept {
    auto* state = static_cast<RenderPluginState*>(opaque);
    if (state == nullptr || !on_owner_thread(*state)) return;
    state->contexts.clear();
    state->windows = nullptr;
  }

  void MOBAGEN_PLUGIN_CALL destroy(void* opaque) noexcept {
    auto* state = static_cast<RenderPluginState*>(opaque);
    if (state == nullptr) return;
    const auto host = state->host;
    state->~RenderPluginState();
    host.deallocate(
        host.host_context, state, sizeof(RenderPluginState),
        alignof(RenderPluginState)
    );
  }

}  // namespace

MOBAGEN_PLUGIN_EXPORT MobagenStatus MOBAGEN_PLUGIN_CALL mobagen_plugin_entry_v1(
    const MobagenHostApiV1* host, MobagenPluginDescriptorV1* descriptor
) {
  static const MobagenStringView provides[] = {
      {MOBAGEN_RENDER_BACKEND_V1_ID,
       sizeof(MOBAGEN_RENDER_BACKEND_V1_ID) - 1}
  };
  static const MobagenStringView required[] = {
      {MOBAGEN_WINDOW_SURFACE_V1_ID,
       sizeof(MOBAGEN_WINDOW_SURFACE_V1_ID) - 1}
  };
  static const MobagenStringView permissions[] = {{"gpu", sizeof("gpu") - 1}};
  if (host == nullptr || descriptor == nullptr
      || host->abi_version != MOBAGEN_PLUGIN_ABI_VERSION
      || host->struct_size < MOBAGEN_PLUGIN_HOST_API_V1_SIZE
      || host->allocate == nullptr || host->deallocate == nullptr
      || descriptor->struct_size < MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE) {
    return MOBAGEN_STATUS_UNSUPPORTED;
  }
  auto* memory = host->allocate(
      host->host_context, sizeof(RenderPluginState), alignof(RenderPluginState)
  );
  if (memory == nullptr) return MOBAGEN_STATUS_OUT_OF_MEMORY;
  auto* state = new (memory) RenderPluginState{
      .host = *host,
      .owner_thread = std::this_thread::get_id(),
  };
  *descriptor = {
      .struct_size = MOBAGEN_PLUGIN_DESCRIPTOR_V1_SIZE,
      .abi_version = MOBAGEN_PLUGIN_ABI_VERSION,
      .id = {"mobagen.render.webgpu", sizeof("mobagen.render.webgpu") - 1},
      .version_major = 1,
      .version_minor = 0,
      .version_patch = 0,
      .reload_policy = MOBAGEN_RELOAD_RESTART,
      .plugin_state = state,
      .provides = provides,
      .provides_count = 1,
      .required = required,
      .required_count = 1,
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
