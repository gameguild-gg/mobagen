#ifndef MOBAGEN_WINDOW_SURFACE_V1_H
#define MOBAGEN_WINDOW_SURFACE_V1_H

#include "plugin_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOBAGEN_WINDOW_SURFACE_V1_ID "window.surface.v1"
#define MOBAGEN_WINDOW_SURFACE_V1_ABI_VERSION UINT32_C(1)

typedef struct MobagenWindowHandleV1 {
  uint32_t index;
  uint32_t generation;
} MobagenWindowHandleV1;

typedef uint32_t MobagenWindowFlagsV1;
#define MOBAGEN_WINDOW_RESIZABLE_V1 (UINT32_C(1) << 0)
#define MOBAGEN_WINDOW_HIGH_PIXEL_DENSITY_V1 (UINT32_C(1) << 1)
#define MOBAGEN_WINDOW_HIDDEN_V1 (UINT32_C(1) << 2)

typedef struct MobagenWindowDescV1 {
  uint32_t struct_size;
  MobagenStringView title;
  int32_t width;
  int32_t height;
  MobagenWindowFlagsV1 flags;
} MobagenWindowDescV1;

typedef uint32_t MobagenNativeSurfaceKindV1;
#define MOBAGEN_NATIVE_SURFACE_NONE_V1 UINT32_C(0)
#define MOBAGEN_NATIVE_SURFACE_WIN32_V1 UINT32_C(1)
#define MOBAGEN_NATIVE_SURFACE_METAL_LAYER_V1 UINT32_C(2)
#define MOBAGEN_NATIVE_SURFACE_WAYLAND_V1 UINT32_C(3)
#define MOBAGEN_NATIVE_SURFACE_XLIB_V1 UINT32_C(4)

/*
 * Platform-neutral transport for a native surface. `display` and `window`
 * are borrowed for the lifetime of the matching window handle. For Xlib,
 * `window_id` carries the integer Window value. For Metal, `window` is the
 * CAMetalLayer pointer owned by the window provider.
 */
typedef struct MobagenNativeSurfaceV1 {
  uint32_t struct_size;
  MobagenNativeSurfaceKindV1 kind;
  void* display;
  void* window;
  uint64_t window_id;
  int32_t width;
  int32_t height;
} MobagenNativeSurfaceV1;

typedef uint32_t MobagenWindowEventTypeV1;
#define MOBAGEN_WINDOW_EVENT_QUIT_V1 UINT32_C(1)
#define MOBAGEN_WINDOW_EVENT_CLOSE_REQUESTED_V1 UINT32_C(2)
#define MOBAGEN_WINDOW_EVENT_RESIZED_V1 UINT32_C(3)

typedef struct MobagenWindowEventV1 {
  uint32_t struct_size;
  MobagenWindowEventTypeV1 type;
  MobagenWindowHandleV1 window;
  int32_t data1;
  int32_t data2;
} MobagenWindowEventV1;

typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenWindowCreateFnV1)(
    void* window_state, const MobagenWindowDescV1* descriptor,
    MobagenWindowHandleV1* window
);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenWindowDestroyFnV1)(
    void* window_state, MobagenWindowHandleV1 window
);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenWindowNativeSurfaceFnV1)(
    void* window_state, MobagenWindowHandleV1 window,
    MobagenNativeSurfaceV1* surface
);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenWindowPollEventFnV1)(
    void* window_state, MobagenWindowEventV1* event
);

typedef struct MobagenWindowSurfaceV1 {
  MobagenCapabilityHeaderV1 header;
  void* window_state;
  MobagenWindowCreateFnV1 create;
  MobagenWindowDestroyFnV1 destroy;
  MobagenWindowNativeSurfaceFnV1 native_surface;
  MobagenWindowPollEventFnV1 poll_event;
} MobagenWindowSurfaceV1;

#define MOBAGEN_WINDOW_HANDLE_V1_SIZE ((uint32_t)sizeof(MobagenWindowHandleV1))
#define MOBAGEN_WINDOW_DESC_V1_SIZE ((uint32_t)sizeof(MobagenWindowDescV1))
#define MOBAGEN_NATIVE_SURFACE_V1_SIZE ((uint32_t)sizeof(MobagenNativeSurfaceV1))
#define MOBAGEN_WINDOW_EVENT_V1_SIZE ((uint32_t)sizeof(MobagenWindowEventV1))
#define MOBAGEN_WINDOW_SURFACE_V1_SIZE ((uint32_t)sizeof(MobagenWindowSurfaceV1))

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MOBAGEN_WINDOW_SURFACE_V1_H */
