# Architecture

![Architecture](assets/architecture.svg)


https://playground.diagram.codes/d/system-layers

```
V[Interfaces,glue,engine,editor] with label "Architecture"

glue=H["Math","Containers &\nData Converters","Log",platform] with label "Glue"
platform=H[mobile,desktop,web] with label "Platform Specific"

mobile=H["android","ios"] with label "Mobile"
desktop=H["win","osx","linux"] with label "Desktop"
web=H["wasm"] with label "WEB"

engine=H[orchestrator,rendering,physics,resource,animation,networking,gameplay] with label "Modules"
rendering=V["GUI","Rendering","Shaders","Scripting","Materials"] with label "Graphics"

physics=V["Particles","Collision","RigidBody","Constraints"] with label "Physics"
animation=V["Skeleton","Poses","Clip","Skinning","Blending","Sprite"] with label "Animation"
orchestrator=V["Time\nManager","Job\nScheduler","IO","Predict\nRollback", "Plugin\nManager"] with label "Orchestrator"

gameplay=V["Scripting","Scene\nGraph","ECS","Events &\nMessaging","AI"] with label "Gameplay"
networking=V["HTTPs","Real-Time"] with label "Networking"
resource=V["Mesh","Audio","Video","Image","Text","Font","Binary","Prefab"] with label "Resource\nManager"

editor=V[editor1] with label "Editor"
editor1=H["Simulator","Visual Scripting","Scene Editor","Animator"]
```

## Module runtime (composition)

MoBaGEn products are assembled from portable **wasm modules** (`.plugin`
packages). There is **no native dynamic-loading (`dlopen`) tier**.

- **Guest modules** are authored against the module ABI (see
  [Modules.md](Modules.md)), compiled to wasm, and packaged as a `.plugin`
  directory with a schema-v2 `module.manifest`. Modules expose coarse-grained
  *capabilities* (init/configure/start/stop/process); the per-frame hot path stays
  host-side.
- **Manager** — `compositions::ProjectModuleManager` is **single-kind**: it owns a
  `compositions::PortableModuleManager`. `open_locked_project` reads the selected
  manifest profile and, for a `wasm` linkage profile, opens the portable manager.
  There is no native/portable kind switch and no native function-table endpoint.
- **Backends** (compile/instantiate wasm):
  - **Native targets** (linux/osx/windows/android/ios) — **WAMR**:
    `plugins::WamrBackend` (wasm interpreter + `wamrc` AOT).
  - **Web / browser engine** — `plugins::BrowserWasmBackend`: the browser
    compiles and instantiates the plugin module; a shared-memory (SAB) host
    services shim bridges the guest.
- **Memory** — `MemoryManager` behind the `MobagenMemoryShim` vtable:
  `native_shim` (WAMR shared-heap + pthread) or `web_shim` (SAB + Atomics, or an
  isolated fallback).

