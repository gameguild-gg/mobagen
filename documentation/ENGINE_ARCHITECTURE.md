# Engine Architecture — Orchestration

> **North star for the engine/editor orchestration layer** — the design we build
> toward, with locked vocabulary. Sits beside the other norths:
> - [ROADMAP.md](ROADMAP.md) — the rendering goal (DICOM volume ray caster).
> - [ARCHITECTURE.md](ARCHITECTURE.md) — where the *code* actually is now.
> - [LEARNING.md](LEARNING.md) — the ordered path.
> - [CONCEPTS.md](CONCEPTS.md) — every idea from zero.
>
> This document is the engine backbone for the **segmentation editor**: how state,
> events, entities, scheduling, and (later) distribution fit together. Modeled on
> master's `core/` (Unity-style `Object/Component/Scene`) but **data-oriented
> (DOD)** — we keep the vocabulary, replace the storage.

---

## 0. The one rule (naming)

We have three things that all sound like "signals/events." **One word = one
concept, zero collisions:**

- **`Signal`** = an Angular-style *reactive value*. Never anything else.
- **`Notifier`** = a Qt/Godot-style *synchronous per-instance callback*.
- **`Event`** = a payload posted on the async **`EventBus`**.
- **`WaitGroup`** = the atomic *job-dependency* tracker a `Fiber` waits on.

If you catch yourself using "signal" for a callback or an event, stop.

The data-oriented core itself is **DOD** (*Data-Oriented Design* — the
Acton/Fabian term), **never "DOTS"** (that is Unity's trademark). DOD = entities
are ids, components live in packed contiguous arrays, systems stream them. It is
the thing that *replaces* the OOP `Object/Component` model in master's `core/`.

---

## 1. The two layers

Everything is either a **consumer** (decides *what/when*) or part of the
**substrate** (decides *how/parallel*). The substrate sits under all consumers.

```
 CONSUMERS  (what / when)
   1 Reactive : Signal · Computed · Effect      → derived STATE
   2 Notifier : Notifier<Args…>                 → sync, per-instance OCCURRENCE
   3 EventBus : Event + EventBus                → async, by-type OCCURRENCE
   4 Systems  : World · Entity · System · View  → iterate DATA
        │                                            ▲
        └──────── kick(work) ───────┐                │ a finished Job can
                                    ▼                │ set a Signal / emit a
 SUBSTRATE  (how / parallel)        │                │ Notifier / post an Event
   JobSystem.kick(job) ─► Job runs on a Fiber ─► returns a WaitGroup
                                    wait(wg): the Fiber SUSPENDS, the worker
                                    thread steals other work, and the Fiber
                                    resumes (maybe on another thread) at 0.
   ─────────────────────────────────────────────────────────────────────
   Platform: native threads | wasm pthreads (workers+SAB)  →  Distributed (later)
```

**The four consumers all use the substrate the same way** — this is the
"fibers + WaitGroups work on all four" property:

| Consumer | kicks work like | can be triggered by a Job result via |
|---|---|---|
| `Effect` | `effect([]{ jobs.kick(rebuildLut); })` | `signal.set(...)` |
| `Notifier` | `onToolPicked.connect([](Tool t){ jobs.kick(...); })` | `notifier.emit(...)` |
| `Event` handler | `bus.subscribe<VolumeLoaded>([](auto&e){ jobs.kick(buildBvh); })` | `bus.post(...)` |
| `System` | a System *is* a `Job`: `parallel_for(view, …)` | (writes components) |

`WaitGroup` pairs with `Fiber` (the wait mechanism); `Signal`/`Notifier`/`Event`
are triggers and results, not waiters.

---

## 2. Reactive — `Signal` / `Computed` / `Effect`  (control-plane STATE)

Angular's model: fine-grained reactive values with **automatic dependency
tracking**. Reading a signal inside a `Computed`/`Effect` records the dependency
(via a thread-local "current consumer"); writing marks dependents dirty; values
recompute lazily on read (glitch-free, pull-after-push).

```cpp
Signal<float> center{40}, width{400};
Signal<int>   preset{0};
auto lut = computed([&]{ return buildLut(center.get(), width.get(), preset.get()); });
effect([&]{ gpu.upload(lut.get()); });   // re-runs whenever any input changes
center.set(20);                           // → lut stale → effect re-uploads
```

- **API:** `Signal<T>`: `.get()` (tracks) / `.set(v)` / `.update(fn)`.
  `Computed<T>`: `.get()`. `Effect`: `effect(fn)`.
- **Use for:** derived editor/parameter state — window/level, transfer-function
  preset, active tool, the inspector's view of the selected entity.
- ⚠️ **Granularity (ROADMAP rule):** control plane only. Tens–thousands of values
  that change *occasionally*. **Never** a `Signal` per voxel/ray/entity-per-frame.

---

## 3. Notifier — `Notifier<Args…>`  (sync, per-instance OCCURRENCE)

A typed multicast callback bound to one object. `connect` listeners; `emit` calls
them **synchronously, in order, right now**. The Qt/Godot "signals & slots" idea,
renamed so it never collides with `Signal`.

```cpp
Notifier<Entity> onSelected;
auto c = onSelected.connect([](Entity e){ inspector.show(e); });
onSelected.emit(vol);        // listeners run inline, now
onSelected.disconnect(c);
```

- **Use for:** local, immediate, instance-specific reactions where ordering and
  "handle it now" matter and the emitter is known.
- **vs EventBus:** Notifier is synchronous + tied to a specific emitter; the bus
  is async + decoupled + by-type. **vs Signal:** Notifier announces an
  *occurrence*; Signal holds *state*.
- **Lifetime:** `connect` returns a `Connection`; disconnect on teardown to avoid
  dangling slots (the one real hazard of the observer pattern).

---

## 4. EventBus — `Event` + `EventBus`  (async, by-type OCCURRENCE)

Decoupled, typed, queued. Producers `post`; they don't know who consumes. The
`Coordinator` drains the queue at defined frame phases. **Compile-time typed — no
string routing** (the ROADMAP "DO NOT USE" rule).

```cpp
struct VolumeLoaded { DicomId id; int w, h, d; };   // coarse, system-boundary
bus.post(VolumeLoaded{...});                         // returns immediately
auto sub = bus.subscribe<VolumeLoaded>([](const VolumeLoaded& e){ renderer.upload(e); });
// drained later: bus.process<VolumeLoaded>();
```

- **Use for:** cross-system, coarse, time-decoupled notifications — `VolumeLoaded`,
  `SceneChanged`, `BvhBuilt`, `FrameCompleted`, `ResizeRequested`.
- ⚠️ **Coarse only.** Never `RayHit` / `PixelSampled` / `EntityMoved`-per-frame.

### Choosing a messaging tool

| The question you're answering | Tool |
|---|---|
| "What *is* X, derived from other state?" | **Signal / Computed** |
| "I read X and want a side-effect when it changes" | **Effect** |
| "*This* object did a thing — handle it now, in order" | **Notifier** |
| "Something happened; whoever cares can handle it later" | **EventBus** |
| Input from the UI (ImGui, immediate-mode) | poll inline → then `set` a Signal or `post` an Event |

> Master's editor uses **ImGui** (immediate-mode): UI input is *polled* each frame
> (`if (ImGui::Button(...))`), so it feeds Signals/Events directly — it is not a
> consumer that needs Notifiers of its own.

---

## 5. ECS — `World` / `Entity` / `System` / `View`  (DATA)

Roll-our-own **sparse-set** ECS (the EnTT default model — *not* archetype). Chosen
for: simplicity (learnable), cheap add/remove (editors toggle components
constantly), and because our entity counts are modest and the bottleneck is the
GPU ray-cast, not CPU iteration.

- **`Entity`** = `index : generation` handle. Generation invalidates stale handles.
- **Storage** = one **sparse set** per component type: packed `dense[]` (values +
  parallel `entities[]`) + `sparse[]` (entity → dense slot). O(1) `add/remove/has/get`,
  packed iteration. We **implement EnTT's architecture ourselves** to own the
  memory/layout (a dependency would hide it):
  - **paged sparse** array (no giant allocation for scattered ids),
  - **chunked-arena dense** store (`Storage<T>`): fixed-size chunks → stable
    pointers, no realloc copies, one cache-friendly run per chunk,
  - **swap-with-last** erase keeps the dense array packed (O(1)),
  - **big data stays out of the ECS** — `VolumeRef`/`MaskRef` hold a handle to a
    GPU texture / arena buffer; the 150 MB voxels never become entities,
  - hot multi-component queries get archetype-like locality via optional **groups**
    (a partitioned, co-ordered subset of pools) — added only where a profile is hot.
  - *Implemented:* `core/sources/ecs/{sparse_set,storage}.hpp` (+ `ecs_demo`). `World`/`View`/
    `Group` and the `ecs_c.h` facade follow.
- **`View<A,B>`** = iterate the *smallest* pool, gate others via sparse lookup.
- **`System`** = a function over a `View`, run by the `Coordinator` (and wrappable
  as a `Job`).
- **Singletons** (active camera, live `VolumeData`, the LUT) live in a small
  resource registry, not as entities.

```cpp
Entity vol = world.create();
world.add<Transform>(vol, {});
world.add<VolumeRef>(vol, {dicomId});
world.view<Transform, VolumeRef>().each([](Transform& t, VolumeRef& v){ /* tight */ });
```

> Archetype ECS (Unity DOTS/Bevy/Flecs) groups entities by exact component set into
> packed chunks — fastest multi-component iteration, but add/remove *moves* an
> entity between chunks (structural change). We don't need that throughput and do
> need cheap toggling, so sparse-set wins here. Revisit only if a profile says so.

---

## 6. Scene graph — `Transform` + `TransformSystem`  (a layer ON the ECS)

The scene graph is **not** a separate tree. `Transform` is a component: local TRS +
`parent` entity + cached world matrix + dirty flag. `TransformSystem` propagates
world matrices top-down with dirty-flag skipping. This reconciles master's
`Transform`/`Scene` with the data-oriented `World`.

Editor entities: a CT volume = `{Transform, VolumeRef, TransferFunction, WindowLevel}`;
a mask = `{Transform, MaskRef, MaterialColor}`; a measurement = `{Transform, LineGizmo}`.

---

## 7. Scheduling — `JobSystem` / `Job` / `Fiber` / `WaitGroup`

The Naughty Dog model (GDC 2015), but with **coroutine-based suspension** (per the
lead researcher). **Worker threads never block on a dependency — they suspend the
job's coroutine and run another.**

> **Coroutines suspend; threads parallelize.** Keep these separate: worker threads
> do the parallel work; a coroutine is how one job *awaits* a `WaitGroup` without
> blocking its worker. They are orthogonal.

- **`JobSystem`** = N worker threads (≈one per core) + a ready-coroutine queue + queues.
- **`Job`** = a coroutine `Task`. `kick(job)` / `kick_batch(jobs) → WaitGroup`.
- **`Fiber`** = our name for a job's *suspendable context*, implemented as a **C++20
  coroutine (stackless)**. At `co_await`, the compiler **auto-saves the coroutine
  frame** (locals + resume point) to the heap and yields the worker — no separate
  stack, no Asyncify, no boost.context; identical on native and web.
- **`WaitGroup`** = atomic countdown. `co_await wg` suspends the coroutine until it
  hits 0; the worker runs other ready coroutines meanwhile; it resumes later
  (possibly on a different thread).

```cpp
Task render() {
    WaitGroup wg = jobs.kick_batch(tiles);   // one job per tile (not per pixel!)
    co_await wg;                              // coroutine suspends; worker steals work
    mergeFilm();
}
```

- **Granularity:** jobs = tiles/batches/BVH-node-ranges. Never per-ray/per-pixel.
- **Stackless trade-off:** you suspend only at `co_await` points *in a coroutine* —
  a deep non-coroutine helper can't yield (it'd have to be a coroutine and be
  `co_await`-ed). For a job system that's a *feature*: suspension points are visible.
- **Hazards (still apply):** a coroutine may resume on a *different* thread, so
  don't hold thread-local state or an OS mutex across a `co_await`.

### Dual-target ("both from day one")

| | Native | Web (WASM) |
|---|---|---|
| Threads | `std::thread` pool, work-stealing deques | Emscripten **pthreads** = Web Workers + **`SharedArrayBuffer`**; needs `-pthread`, a worker pool |
| Fiber (suspension) | **C++20 coroutine** `Task` | **same C++20 coroutine** — compiles to wasm, *no Asyncify, no boost.context* |
| WaitGroup | `std::atomic<int>` | same (atomics over SAB) |
| Serving | — | page **must** send COOP `same-origin` + COEP `require-corp` (plain `python -m http.server` won't) → ship a tiny dev server |
| Fallback | — | **single-thread inline executor** when SAB is unavailable — non-negotiable for a web deliverable |

> Coroutines handle *suspension* identically on both targets. Only the *threading*
> differs (native `std::thread` vs Emscripten pthreads). The prototype
> (`apps/fibers_prototype/`) validated the model: **`coro_demo` (stackless) is the
> canonical basis**; `fiber_demo` (stackful Win32) stays as the educational contrast
> — the road not taken (and why: it needs a stack per fiber + Asyncify on web).

---

## 8. Distributed — designed now, built last

The outermost layer. Use cases for the editor: heavy segmentation (region-growing,
ML inference), large-volume preprocessing, multi-node rendering.

- **Seam:** a `Job` can be *remote* — same `JobSystem` interface, dispatched over a
  transport to a worker process/node, operating on **serialized component slices**
  from the `World` (which stays the source of truth).
- **Web reality:** real distribution = **client ↔ compute server** (WebSocket/WebRTC);
  the browser can't do MPI. Native could do true multi-node later.
- **Build:** the `JobSystem` backend abstraction + a serialization boundary now;
  the transport later.

---

## 9. Modules & the C ABI

The engine is **module-based** (the "MoBaGEn" in the name): each subsystem is a
library whose **contract is a C ABI facade** (`*_c.h`, `extern "C"`). Same boundary
we already ship for `volume_io` and `engine_c` (the study module) — now applied
engine-wide. It buys decoupling, ABI stability across compilers, language-agnostic
/ scriptable access, swappable implementations, and "a module I can call from
another core to study."

**The rule — granularity applies to the ABI exactly as to events/jobs:**
- **C ABI at module seams (coarse):** `world_create`, `jobs_kick(batch)`,
  `eventbus_post`, `volume_io_load_series`, `engine_step`. Low frequency → free.
- **C++ inside a module (hot / generic):** `view<A,B>().each(...)`, `signal.get()`
  per read, ray batches. **Never cross `extern "C"` per-entity/per-ray** — it
  erases generics *and* destroys throughput.
- **Escape hatch for cross-module hot access:** the ABI returns a raw *span*
  (pointer + count + stride); the caller makes one ABI call, then a tight C++ loop.

**Two patterns every module uses:**
```c
// 1. Opaque handle — type incomplete in the header; callers never see the layout
typedef struct World World;
World* world_create(void);   void world_destroy(World*);

// 2. Callback = function pointer + void* user (C ABI can't take a C++ lambda)
typedef void (*JobFn)(void* user);
WaitGroup* jobs_kick(JobSystem*, JobFn fn, void* user);  // C++ wrapper packs the lambda
```

**Each module = three files:**
```
core/<module>/
  <module>_c.h     C ABI facade — the cross-module CONTRACT (opaque handles)
  <module>.hpp     ergonomic C++ API (templates) — in-module + hot-path use
  <module>.cpp     implementation
```

**Layout:**
```
core/sources/    shared engine code: reactive, messaging, ecs, scene, render,
                 jobs, input, net, resource, camera
modules/         optional app-consumable modules: volume, volume_io,
                 opengl_renderer
apps/            executables: dicom_viewer, core_demos, volume_demos,
                 dawn_probe, fibers_prototype
```

**What crosses each ABI (coarse) vs stays C++ (hot):**

| Module | ABI facade (coarse) | C++ inside (hot / generic) |
|---|---|---|
| jobs | create · kick · parallel_for · wait(WaitGroup) | work-stealing, fiber switching |
| messaging | eventbus_post/subscribe/process (type-erased) | `Notifier<Args…>`, typed `post<T>()` |
| ecs | entity create · add/get/remove · **pool span** | `view<…>().each(...)`, systems |
| reactive | create/set/read type-erased signal (scripting) | `Signal<T>`/`Computed<T>`/`Effect` graph |
| scene | set_parent · world_matrix(entity) | `TransformSystem` propagation |
| render | (next) expose draw-command spans | `RenderBridge`, GPU-resource handles |
| volume_io ✓ | load_series → VolumeData · free | GDCM internals |
| engine ✓ | create · step · run · wire modules | per-frame system schedule |

---

## 10. Build order (dependency-forced)

1. `core/engine` scaffold + `Coordinator` loop (native + wasm entry).
2. **Reactive** (`Signal`/`Computed`/`Effect`) — smallest, target-agnostic. ◀ start
3. **Messaging** (`Notifier`, then `Event`/`EventBus`).
4. **ECS** (`World`/`sparse_set`/`View`/`System`).
5. **Scene** (`Transform` + `TransformSystem`).
6. **Render bridge** (`VolumeRenderable` -> flat draw commands).
7. **JobSystem** dual-target (`Job`/`Fiber`/`WaitGroup`) + COOP/COEP dev server +
   single-thread fallback.
8. **Distributed** seam.

Each rung ships its module as the three-file pattern (`<module>_c.h` facade +
`<module>.hpp` + `.cpp`) plus a tiny demo proving it. Each is target-agnostic
until #6, which is where "both from day one" applies.

---

## Decisions log

| Decision | Choice | Why |
|---|---|---|
| Reactive model | Angular `Signal`/`Computed`/`Effect` | auto-derivation; control plane only |
| Observer model | keep, named **`Notifier`** | sync per-instance callbacks (vs async bus) |
| Events | `EventBus`, compile-time typed | decoupled, coarse, no string routing |
| ECS storage | roll-our-own **sparse-set** | simple, cheap add/remove, learnable |
| Scene graph | `Transform` component + system on ECS | not a separate tree |
| Fibers (suspension) | **C++20 coroutines (stackless)** — `Fiber` = a coroutine `Task` | researcher's call: auto-saves the frame, portable to wasm with no Asyncify/boost; suspension ≠ parallelism (threads do that) |
| Job dependency | **`WaitGroup`** | clearest name (Go lineage) |
| Concurrency target | native + web from day one | with single-thread fallback |
| Distributed | seam now, transport later | de-risk without over-building |
| Module boundaries | **C ABI facade** (`*_c.h`, opaque handles + fn-ptr callbacks) | decouple / swap / script; coarse seams only — C++ in hot loops |
