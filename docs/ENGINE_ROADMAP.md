# Engine Study Roadmap (Core)

> Current render-bridge status: Dawn/WebGPU now consumes `VolumeDrawCommand`
> directly. The renderer binds embedded `raygen.wgsl`, camera/window/mode/debug
> uniforms, a 3D volume texture, and a transfer LUT. The native `USE_GDCM=ON`
> path can now replace the phantom bytes with DICOM stored UInt16 data packed by
> `VolumeBuffer` into RG8 for WebGPU upload; WGSL reconstructs the stored value
> and applies window/level on the GPU. A first WebGPU compute histogram pass now
> builds scalar bins and feeds auto-windowing. The browser path still uses the raw
> phantom until we decide how heavy a WASM DICOM parser dependency should be.

> **A teaching roadmap, not just a todo list.** This project is building a
> **performance-driven engine core** (a modular, dual-target engine backbone) —
> *not* a renderer right now. Each module is a learning unit: what it is, the
> concepts it teaches, its data structures, and how we make it *fast*. Build
> order, design, and vocabulary live in [ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md);
> this doc is the curriculum + status + measurements.

---

## The performance creed (how we work)

1. **Measure, don't guess.** Every performance-relevant module ships a benchmark.
   A claim of "fast" without a number is a TODO.
2. **Granularity rule.** Coordination (jobs, events, signals, the C ABI) belongs at
   *coarse* boundaries; hot loops stay tight plain C++. Cost of scheduling must be
   « cost of the work. (We *measured* this: see Jobs below — the 1-worker scheduler
   is slower than a plain loop; you only win with coarse grain + enough cores.)
3. **Lock-free where it counts** (per-worker deques), simple locks where it doesn't
   (the rarely-touched global injection queue).
4. **Cache-friendly layout** (sparse-set + chunked arena; big data outside the ECS).
5. **Suspension ≠ parallelism.** Coroutines suspend a job; threads parallelize.

---

## Migration to master (greenfield, port the editor last)

The engine core is **DOD** (*Data-Oriented Design* — entities are ids, components
live in packed arrays, systems stream them). master's `core/` is the older OOP
`Object/Component/Scene` model. Goal: the engine runs on DOD.

**Chosen path: greenfield — do NOT rewrite master's core in place** (that breaks
the editor mid-flight). Instead:
1. **Build the DOD engine out fully** under its own tree (`orchestration/` on the
   integration branch; `core/` here): jobs ✅ ecs ✅ reactive ✅ messaging ✅
   scene ✅ net ✅ → then the capabilities the editor needs (below).
2. **Keep master's OOP `core/` + editor running, untouched,** the whole time. The
   `integration/orchestration-on-master` branch already does this: pure addition,
   `BUILD_ORCHESTRATION` off by default — master's build + CI unchanged.
3. **Last phase:** port the editor (+ examples/modules) onto the DOD core, then
   retire the OOP core.

Capabilities still owed before the editor can sit on DOD: a stable **entity/scene
API + serialization**, **input**, and **resource/asset** handling. The first
render bridge rung now exists: DOD volume entities can be flattened into renderer
commands; the next step is making Dawn consume those commands.

---

## Modules (curriculum)

Each: **Goal · Learn · Data structures · Perf · Status.**

### reactive — `Signal` / `Computed` / `Effect`
- **Goal:** auto-derived editor state (window/level → LUT, selection → inspector).
- **Learn:** reactive dependency graphs, auto-tracking, push-invalidate / pull-recompute.
- **Data structures:** signal nodes + dependent lists; a thread-local "current consumer".
- **Perf:** control-plane only — *never* per-voxel/ray. Bookkeeping cost is fine for
  tens–thousands of occasionally-changing values.
- **Status:** ✅ built (`core/sources/reactive/reactive.hpp`, `reactive_demo`). Eager push model
  with auto-tracking + change-detection; verified (center.set → Computed recomputes →
  Effect re-fires). Enhancements deferred: lazy/glitch-free pull, dynamic-dep cleanup,
  unsubscribe-on-destroy, `reactive_c.h`.

### messaging — `Notifier` + `Event`/`EventBus`
- **Goal:** `Notifier` = sync per-instance callback; `EventBus` = async, by-type, decoupled.
- **Learn:** observer vs queued events; compile-time-typed dispatch (no string routing).
- **Perf:** coarse events only (`VolumeLoaded`, not `RayHit`). EventBus drained at frame phases.
- **Status:** ✅ built (`core/sources/messaging/{notifier,event_bus}.hpp`, `messaging_demo`). Notifier
  sync multicast (connect/emit/disconnect); EventBus async by-type (post queues, process drains),
  per-type channel, no string routing. Verified. `events_c.h` facade deferred.

### ecs — `World` / `Entity` / `View` / `Group`
- **Goal:** data model for scene objects (volumes, masks, tools).
- **Learn:** sparse-set vs archetype; entity generations; views; groups for locality.
- **Data structures:** **paged sparse array** + **chunked-arena `Storage<T>`**; swap-with-last erase.
- **Perf:** O(1) add/remove; packed iteration; stable pointers (no realloc churn);
  **big data lives outside the ECS** (a `VolumeRef` holds a GPU/arena handle).
- **Status:** ✅ **storage core** (`sparse_set`/`Storage<T>`, `ecs_demo`) — contiguous /
  stable / packed. ✅ **World** (`world.hpp`, `world_demo`) — generation-checked entities,
  index recycling, add/get/has/remove, single + two-component views, type-erased destroy,
  tags. ✅ **systems-as-jobs** (`apps/core_demos/sources/composition/systems_demo`): a movement system over
  **2M entities** runs **6.78× faster** through `Scheduler::parallel_for` + `World::apply_range`
  than the serial `view`, results verified identical — modules stay decoupled (the app glues
  them). ⏳ Group (locality) + `ecs_c.h` facade.

### scene — `Transform` + `TransformSystem`
- **Goal:** scene graph as a layer *on* the ECS (not a separate tree).
- **Learn:** hierarchy as components; world-matrix propagation; scene graph as a *system*.
- **Status:** ✅ built (`core/sources/scene/{transform,transform_system}.hpp`, `scene_demo`). Transform
  = local TRS + parent + cached world (GLM); TransformSystem resolves world = parent.world *
  local (recursive + per-pass memo). Verified (move root → subtree follows). Dirty-subtree skip
  + parallelize-over-scheduler are later optimizations.

### render — `RenderBridge` / `VolumeRenderable`
- **Goal:** turn DOD scene data into flat renderer commands without making the ECS
  depend on WebGL, Dawn, ImGui, or DICOM parser internals.
- **Learn:** render bridges, coarse data handoff, GPU-resource handles, why big
  voxel arrays stay outside the ECS.
- **Data structures:** `VolumeRenderable` ECS component + compact
  `VolumeDrawCommand[]` output. A volume entity is `{Transform, VolumeRenderable}`;
  the command stores the resolved world matrix, volume resource id, dimensions,
  voxel spacing, scalar format, window/level, transfer preset, and render mode.
- **Status:** ✅ skeleton built (`core/sources/render/render_bridge.hpp`,
  `render_bridge_demo`). It proves the DOD -> renderer seam with a DICOM-like
  `512x512x300` UInt16 CT volume. The Dawn host now consumes the bridge command
  for the volume ray-cast path.

### jobs — `Task` / `WaitGroup` / `Scheduler` (coroutine work-stealing)  ⭐ active
- **Goal:** a *fast* parallel job system — the engine's performance heart.
- **Learn:** C++20 coroutines (stackless, `co_await` auto-saves the frame); work-stealing
  (LIFO-own / FIFO-steal); the **Chase-Lev** lock-free deque + memory fences; fork-join
  with `WaitGroup`; the park-vs-finish race; cross-thread resume.
- **Data structures:** per-worker `ChaseLevDeque<>` (lock-free) + a global injection queue;
  `WaitGroup` (atomic countdown + parked-handle list); coroutine `Task`.
- **Perf (measured fork-join, 64M elements, 12-thread box; before → after the
  per-task optimizations):**
  | workers | before opt | after opt |
  |---|---|---|
  | serial loop | 133 ms | ~97 ms (same code; ~±25% run variance) |
  | 1 | 244 ms (0.55× serial) | **95 ms (≈1.0× — overhead gone)** |
  | 2 | 206 ms (0.65×) | 56 ms (1.7×) |
  | 8 | 20.5 ms (6.5×) | 15.4 ms (≈6.3×) |
  Per-worker throughput **275 → 703 M items/s**.
  **Lesson:** per-task overhead (coroutine-frame malloc, `WaitGroup` mutex, hot loop
  *inside* a coroutine body) made 1 worker *slower* than a plain loop. Fixing all
  three — **frame pool allocator**, **lock-free single-waiter WaitGroup**, **hot loop
  in a plain function** — made the 1-worker scheduler free. Measurement is noisy
  (turbo/background): trust within-run comparisons; rigorous curves need averaged,
  clock-pinned runs.
- **Status: ✅ COMPLETE (5a–5d).** ✅ 5a scheduler+demo, ✅ 5b Chase-Lev lock-free +
  wake + bench, ✅ 5b-opt frame pool + lock-free WaitGroup + hot-loop extraction,
  ✅ 5c dual-target (`Mode::Inline` fallback + `scripts/serve.py` COOP/COEP + Emscripten
  pthread build `build/wasm-jobs/bin/jobs_bench.html`), ✅ 5d `parallel_for` + `wait` +
  `jobs_c.h` C ABI facade (`jobs_c_demo` drives it via the C contract alone).

### net — distributed
- **Goal:** offload heavy compute (segmentation, preprocessing) to workers/nodes.
- **Learn:** a `Job` as a remote unit; serialized component slices; client↔server on web.
- **Perf/seam:** a `JobSystem` backend + a serialization boundary; transport last.
- **Status:** ✅ seam built (`core/sources/net/transport.hpp`, `net_demo`). `ITransport` +
  `LoopbackNode` (in-process node thread) + POD (de)serialization; offloaded a sum to
  a node and verified. Swap LoopbackNode for a socket/WebSocket = real distribution.

---

## Concepts glossary (what we've learned)

- **C++20 coroutine (stackless):** a function the compiler turns into a state machine;
  `co_await` saves its frame (locals + resume point) to the heap. Used as our `Task`/`Fiber`.
- **promise_type / awaiter:** the coroutine's control hooks; `await_ready/suspend/resume`
  decide suspension. Our `WaitGroup::Awaiter` parks the handle.
- **Work-stealing:** per-worker deques; idle workers steal. Owner works the **bottom**
  (LIFO, cache-hot); thieves take the **top** (FIFO, biggest chunk). Auto load-balancing.
- **Chase-Lev deque:** the lock-free deque enabling the above. `push/pop` (owner) are
  almost CAS-free; `steal` is one CAS; the last-element race is a CAS. Needs `seq_cst`
  fences on weak memory models — the price of lock-free.
- **Memory orders:** `relaxed` (no ordering), `acquire`/`release` (pairwise sync),
  `seq_cst` (global order). The deque needs them to be correct without locks.
- **WaitGroup / fork-join:** atomic countdown; `co_await wg` suspends until 0. The
  park-vs-finish race is closed under one mutex (recheck while holding it).
- **Sparse-set / paged sparse / chunked arena / swap-with-last:** the ECS storage
  tricks — O(1) ops, packed iteration, stable pointers.
- **Scaling vs overhead:** speedup is honest only against a *serial* baseline; a job
  system has fixed per-task cost, so coarse grain + enough cores are required to win.

---

## Status snapshot

| Area | State |
|---|---|
| Build infra (CMake, GDCM v3.0.24, sanitizers, CPMLicenses) | ✅ |
| DICOM loader (`volume_io`, native) | ✅ (`make dicom-smoke`; WebGPU upload preserves UInt16 as packed RG8) |
| GPU histogram + auto-window | ✅ first pass (`histogram.wgsl` compute bins + CPU percentile readback) |
| Orchestration design + locked vocabulary | ✅ ([ENGINE_ARCHITECTURE.md](ENGINE_ARCHITECTURE.md)) |
| ECS storage core | ✅ |
| **Jobs module (5a–5d): work-stealing + lock-free + dual-target + C ABI** | ✅ **complete** |
| ECS: storage + World (entities/views/recycle) | ✅ |
| **systems-as-jobs** (ECS view parallelized on the scheduler) | ✅ **6.78× on 2M entities, verified** |
| reactive (Signal/Computed/Effect) | ✅ (auto-track + change-detect, verified) |
| messaging (Notifier sync + EventBus async) | ✅ (verified) |
| scene (Transform + TransformSystem on ECS) | ✅ (hierarchy propagation verified, GLM) |
| render bridge (VolumeRenderable -> VolumeDrawCommand) | ✅ (DOD-to-render seam consumed by Dawn WebGPU host) |
| **engine composition** (`engine_demo`: all modules in one tick) | ✅ (reactive+events+ECS+jobs+scene) |
| net / distributed seam (transport + serialization) | ✅ (offload verified) |
| **ORCHESTRATION BACKBONE** | ✅ **complete** |
| reactive / messaging / scene / net | ⏳ |

Build the core libraries: `make core`. Build the core demos/benches separately:
`make core-examples`.
then run `build/native/bin/Release/{ecs_demo,jobs_demo,jobs_bench}.exe`.
