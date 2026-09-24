CPP code:
apps        -> all demos, dicom viewer, game engine scene editor, etc,
core        -> all code shared with all apps,
modules     -> guest wasm modules (packaged as .plugin),
sdk         -> public headers + module authoring SDK (module ABI),

Non CPP:
htmls                       -> bundle targets / web glue,
apps/appname/assets         -> assets specific for that app,
apps/appname/shaders        -> shaders specifics for that app,
external                    -> all external dependencies (fetched via CPM),
core/shaders                -> all shaders that should be bundled to all apps,
core/assets                 -> all assets that should be bundled to all apps,
core/sources                -> all hpp, cpp for core related code,
  core/sources/ecs/         -> Entity-Component-System (World, Storage, SparseSet)
  core/sources/jobs/        -> Work-stealing scheduler, coroutine tasks, Chase-Lev deque
  core/sources/reactive/    -> Signal/Computed/Effect reactive state
  core/sources/messaging/   -> Notifier + EventBus
  core/sources/scene/       -> Transform hierarchy + TransformSystem
  core/sources/camera/      -> Orbit/WASD camera
  core/sources/input/       -> SDL-agnostic input state
  core/sources/render/      -> RenderBridge (ECS to draw commands)
  core/sources/app/         -> Application host: SDL3 callback loop, WebGPU context, render modes (windowed/headless-null/headless-none)
  core/sources/imgui/       -> ImGui GUI layer for the app host (ImGui_ImplSDL3 + ImGui_ImplWGPU)
  core/sources/rmlui/       -> RmlUi GUI layer for the app host (ImGui-bridge rendering)
  core/sources/resource/    -> ResourceRegistry<T> stable-handle asset store
  core/sources/net/         -> Networking (gated)
  core/sources/memory/      -> MemoryManager + MobagenMemoryShim (native_shim / web_shim)
  core/sources/modules/     -> module manifest (schema v2), lockfile, descriptors
  core/sources/plugins/     -> wasm plugin loader + backends (WamrBackend native, BrowserWasmBackend web)
  core/sources/datastructures/ -> Grid2D, Tree, Vector, concepts
apps/module-compositions    -> module manager composition apps (portable manager, graphics/headless composition)
scripts                     -> all automation (build.py, toolchains.py, smoke_web.mjs, baseline.py),
test                        -> ctest suite (foundation, module e2e, app host),
tools                       -> catalog publisher etc,
benchmarks                  -> foundation/module/memory benchmark suites,
documentation               -> all documentation (Architecture.md, Modules.md),
platforms/android           -> boilerplate for android
