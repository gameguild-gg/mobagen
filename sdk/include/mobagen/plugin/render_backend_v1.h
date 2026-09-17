#ifndef MOBAGEN_RENDER_BACKEND_V1_H
#define MOBAGEN_RENDER_BACKEND_V1_H

#include "plugin_abi.h"
#include "window_surface_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOBAGEN_RENDER_BACKEND_V1_ID "render.backend.v1"
#define MOBAGEN_RENDER_BACKEND_V1_ABI_VERSION UINT32_C(1)

typedef struct MobagenRenderContextHandleV1 {
  uint32_t index;
  uint32_t generation;
} MobagenRenderContextHandleV1;

typedef uint32_t MobagenRenderPowerPreferenceV1;
#define MOBAGEN_RENDER_POWER_DEFAULT_V1 UINT32_C(0)
#define MOBAGEN_RENDER_POWER_LOW_V1 UINT32_C(1)
#define MOBAGEN_RENDER_POWER_HIGH_V1 UINT32_C(2)

typedef uint32_t MobagenRenderBackendTypeV1;
#define MOBAGEN_RENDER_BACKEND_DEFAULT_V1 UINT32_C(0)
#define MOBAGEN_RENDER_BACKEND_NULL_V1 UINT32_C(1)

typedef struct MobagenRenderContextDescV1 {
  uint32_t struct_size;
  MobagenWindowHandleV1 window;
  MobagenRenderPowerPreferenceV1 power_preference;
  MobagenRenderBackendTypeV1 backend_type;
  uint32_t want_surface;
} MobagenRenderContextDescV1;

/* Raw WebGPU handles are borrowed and remain owned by the render provider. */
typedef struct MobagenRenderBackendHandlesV1 {
  uint32_t struct_size;
  void* device;
  void* queue;
  void* surface;
  uint32_t surface_format;
} MobagenRenderBackendHandlesV1;

typedef uint32_t MobagenRenderFrameStatusV1;
#define MOBAGEN_RENDER_FRAME_OPTIMAL_V1 UINT32_C(0)
#define MOBAGEN_RENDER_FRAME_SUBOPTIMAL_V1 UINT32_C(1)
#define MOBAGEN_RENDER_FRAME_TIMEOUT_V1 UINT32_C(2)
#define MOBAGEN_RENDER_FRAME_OUTDATED_V1 UINT32_C(3)
#define MOBAGEN_RENDER_FRAME_LOST_V1 UINT32_C(4)
#define MOBAGEN_RENDER_FRAME_ERROR_V1 UINT32_C(5)

typedef struct MobagenRenderFrameV1 {
  uint32_t struct_size;
  void* texture;
  MobagenRenderFrameStatusV1 status;
} MobagenRenderFrameV1;

typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenRenderCreateContextFnV1)(void* render_state, const MobagenRenderContextDescV1* descriptor,
                                                                           MobagenRenderContextHandleV1* context);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenRenderDestroyContextFnV1)(void* render_state, MobagenRenderContextHandleV1 context);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenRenderGetHandlesFnV1)(void* render_state, MobagenRenderContextHandleV1 context,
                                                                        MobagenRenderBackendHandlesV1* handles);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenRenderConfigureSurfaceFnV1)(void* render_state, MobagenRenderContextHandleV1 context, int32_t width,
                                                                              int32_t height);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenRenderAcquireFrameFnV1)(void* render_state, MobagenRenderContextHandleV1 context,
                                                                          MobagenRenderFrameV1* frame);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenRenderReleaseFrameFnV1)(void* render_state, MobagenRenderFrameV1 frame);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenRenderPresentFnV1)(void* render_state, MobagenRenderContextHandleV1 context);
typedef MobagenStatus(MOBAGEN_PLUGIN_CALL* MobagenRenderTickFnV1)(void* render_state, MobagenRenderContextHandleV1 context);

typedef struct MobagenRenderBackendV1 {
  MobagenCapabilityHeaderV1 header;
  void* render_state;
  MobagenRenderCreateContextFnV1 create_context;
  MobagenRenderDestroyContextFnV1 destroy_context;
  MobagenRenderGetHandlesFnV1 get_handles;
  MobagenRenderConfigureSurfaceFnV1 configure_surface;
  MobagenRenderAcquireFrameFnV1 acquire_frame;
  MobagenRenderReleaseFrameFnV1 release_frame;
  MobagenRenderPresentFnV1 present;
  MobagenRenderTickFnV1 tick;
} MobagenRenderBackendV1;

#define MOBAGEN_RENDER_CONTEXT_HANDLE_V1_SIZE ((uint32_t)sizeof(MobagenRenderContextHandleV1))
#define MOBAGEN_RENDER_CONTEXT_DESC_V1_SIZE ((uint32_t)sizeof(MobagenRenderContextDescV1))
#define MOBAGEN_RENDER_BACKEND_HANDLES_V1_SIZE ((uint32_t)sizeof(MobagenRenderBackendHandlesV1))
#define MOBAGEN_RENDER_FRAME_V1_SIZE ((uint32_t)sizeof(MobagenRenderFrameV1))
#define MOBAGEN_RENDER_BACKEND_V1_SIZE ((uint32_t)sizeof(MobagenRenderBackendV1))

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MOBAGEN_RENDER_BACKEND_V1_H */
