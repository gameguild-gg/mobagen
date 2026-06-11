CPP code:
apps    -> all demos, dicom viewer, game engine scene editor, etc,
core    -> all code shared with all apps,
modules -> modules can be included by the apps (volume, volume_io),

Non CPP:
htmls,
apps/appname/assets  -> assets specific for that app,
apps/appname/shaders -> shaders specifics for that app,
external -> all external dependencies,
core/shaders  -> all shaders that should be bundled to all apps,
core/assets   -> all assets that should be bundled to all apps,
core/sources  -> all hpp, cpp for core related code,
  core/sources/ecs/        -> Entity-Component-System (World, Storage, SparseSet)
  core/sources/jobs/       -> Work-stealing scheduler, coroutine tasks, Chase-Lev deque
  core/sources/reactive/   -> Signal/Computed/Effect reactive state
  core/sources/messaging/  -> Notifier + EventBus
  core/sources/scene/      -> Transform hierarchy + TransformSystem
  core/sources/camera/     -> Orbit/WASD camera
  core/sources/input/      -> SDL-agnostic input state
  core/sources/render/     -> RenderBridge (ECS to draw commands)
  core/sources/resource/   -> ResourceRegistry<T> stable-handle asset store
  core/sources/net/        -> Networking (gated)
  core/sources/datastructures/ -> Grid2D, Tree, Vector, concepts
scripts  -> all automation,
documentation     -> all documentation goes here,
platforms/android  -> boilerplate for android