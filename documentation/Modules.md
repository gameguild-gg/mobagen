# Module Authoring Guide

This document is the reference for writing, packaging, and loading MoBaGEn wasm
modules (`.plugin` packages). It covers the module API, manifest schema, memory
model, guest toolchain requirements, and deployment constraints.

---

## Module ABI (v1)

### Entry Symbol

Every module exports a single entry function:

```c
MOBAGEN_MODULE_EXPORT MobagenModuleStatus MOBAGEN_MODULE_CALL
mobagen_module_entry_v1(const MobagenModuleHostApiV1* host,
                        MobagenModuleDescriptorV1* descriptor);
```

- **File**: `sdk/include/mobagen/module/module_abi.h` (lines 198–201)
- The host calls this after instantiation. The guest verifies
  `host->struct_size >= MOBAGEN_MODULE_HOST_API_V1_SIZE` and
  `host->abi_version == MOBAGEN_MODULE_ABI_VERSION` before using any field.
- The guest fills `descriptor->threads_policy` to declare its threading model.

### Annotation Macros

Exported functions are annotated in a compile-time table:

```c
MOBAGEN_MODULE_EXPORT_TABLE_BEGIN(my_table, 2)
MOBAGEN_MODULE_EXPORT_ENTRY(my_table, add, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32, MOBAGEN_MODULE_T_I32)
MOBAGEN_MODULE_EXPORT_ENTRY(my_table, greet, MOBAGEN_MODULE_T_VOID, MOBAGEN_MODULE_T_PTR)
MOBAGEN_MODULE_EXPORT_TABLE_END(my_table, add, greet)
MOBAGEN_MODULE_EXPORT_PUBLISH_TABLE(my_table)
```

- **Macros**: `MOBAGEN_MODULE_EXPORT_TABLE_BEGIN`, `MOBAGEN_MODULE_EXPORT_ENTRY`, `MOBAGEN_MODULE_EXPORT_TABLE_END`, `MOBAGEN_MODULE_EXPORT_PUBLISH_TABLE`
- **File**: `sdk/include/mobagen/module/module_abi.h` (lines 203–321)
- `TABLE_BEGIN(name, capacity)` declares the table type and capacity.
- `ENTRY(name, fn, ret, ...)` annotates a function with its signature-id (up to 5 params).
- `TABLE_END(name, fn1, fn2, ...)` assembles the table and static-asserts the entry count matches capacity.
- `PUBLISH_TABLE(name)` makes the table discoverable: it declares a global `mobagen_module_exports_v1` that wasm-ld exports as an immutable wasm global (the extraction tool reads it).
- Table capacity is capped at 9 entries (widened in T18).

### Export Table Struct

```
MobagenModuleExportTableV1 {
  uint32_t kind;          // 0x4D313242 ("M12B")
  uint32_t abi_version;   // 1
  uint32_t entry_count;
  uint32_t reserved;
  MobagenModuleExportEntryV1 entries[capacity];
}
```

Each entry carries `name` (offset into linear memory), `fn` (wasm function pointer), `signature_id`, and `reserved`.

### Signature-ID Encoding (32-bit)

```
[31:24] magic 0x4D
[23:20] parameter count (0..5)
[19:17] return type code
[16:2]  parameter codes, 3 bits each (5 slots, first at bit 14)
[1:0]   reserved zero
```

Type codes: `VOID=0, I32=1, I64=2, F32=3, F64=4, PTR=5, SPAN=6`.

Convenience macros: `MOBAGEN_MODULE_SIG_0` through `MOBAGEN_MODULE_SIG_5`.

### Host Imports

Modules import host services under the canonical module name `"mobagen_module_v1"` (`MOBAGEN_MODULE_IMPORT_MODULE_V1`). The host resolves:

- `mobagen_thread_spawn_v1` — only for `threads: managed` modules (T1).

---

## Module Manifest (schema v2)

The manifest lives at `module.manifest` inside the `.plugin` package directory.

### Fields

| Field | Type | Description |
|---|---|---|
| `schema` | u32 | Always `2` |
| `api_version` | u32 | Host API version (currently 1) |
| `abi_version` | u32 | Module ABI version (currently 1) |
| `entry` | string | Entry symbol (e.g. `"mobagen_module_entry_v1"`) |
| `threads` | string | `"none"` or `"managed"` |
| `shared_memory` | bool | Whether the guest requires shared-memory capability |
| `toolchain` | object | `{ producer: "wamrc"\|"emcc"\|"wasi-clang", version: "2.4.5" }` |
| `exports` | list | Per-export `{ name, signature_id }` |
| `payloads` | list | Per-file `{ filename, hash: "sha256:...", size }` |

- **Schema + parser**: `core/sources/modules/module_manifest.hpp` (lines 169–179), `core/sources/modules/module_manifest.cpp`
- **Signature validation**: mirrors `module_abi.h` decode rules (magic 0x4D, ≤5 params, low 2 bits zero)
- **Package manifest hash**: `module_manifest_signature()` = SHA-256 over canonical `name:signature_id\n` lines

### Threads Policy

- **`threads: none`** (default): The module cannot spawn threads. The host resolves no thread-spawning import, so spawning is impossible by construction. Allocation safe points suffice for GC.
- **`threads: managed`**: The module may spawn threads ONLY through `mobagen_thread_spawn_v1` (resolved only for managed modules). It MUST export `mobagen_module_thread_quiesce_v1` so the GC can park each guest thread before marking.

### Shared Memory

`shared_memory: true` indicates the guest was compiled with atomics + bulk-memory capability. The runtime (WAMR shared-heap or web SAB) requires this to be set. The loader enforces it for shared-heap runtimes (issue: `SharedMemoryCapabilityMissing`).

---

## Memory Model

### Region

Each module instance owns one contiguous region of linear memory managed by `MemoryManager`. The region layout:

```
[ control block (64 bytes) | handle table | object chunks ... ]
```

- **File**: `core/sources/memory/memory_manager.hpp` (lines 35–39)
- **Control block** (`+0..64`): magic "MMG1", epoch (bit 0 = GC in flight), host_park, live_objects count, in_flight bracket (+16, mutator count), diagnostics.
- **Handle table** (`+64`): 4096 slots × 8-byte stride. Each slot: `[block_offset:u32 | generation|root_flag:u32]`. Handle encoding: `[31:16] generation (starts at 1), [15:0] slot index`. Null handle = 0x00000000.
- **Object chunks**: carved by size class. Class `i` serves payloads up to `(16 << i)` bytes (11 classes, 16B to 8KB). Each block: 16-byte header (`magic_and_version | flags | payload_bytes | ref_count`) + payload.

- **File**: `core/sources/memory/object_header.h`

### Allocation

`alloc(payload_bytes, ref_count)` → block offset. Allocation is a **safe point**: if the GC epoch is odd (collect in flight), the allocating thread parks via `word_wait` until the epoch flips even, then retries. Auto-collects when the size class free list is empty.

### Handles

- `handle_for(block_offset)` → deterministic handle (same alloc sequence → same handles)
- `handle_resolve(handle)` → current block offset, or null if generation is stale
- Stale handles (from swept objects) die via generation bump on sweep-release

### Roots

- `register_root(handle)` marks the handle as in-registry: the object stays reachable
- `unregister_root(handle)` removes it
- The mark phase walks root handles → resolved blocks → payload ref words → transitively reachable blocks

### GC (Tracing Mark-Sweep)

The collect sequence (quiesce-first, `core/sources/memory/memory_manager.cpp` line 411):

1. **Quiesce managed guest threads** (before ANY epoch flip). Timeout/failed → loud abort, heap untouched.
2. **CAS epoch even→odd** (park-and-retry defensively if odd). Bump host_park.
3. **Drain in_flight bracket** to zero (word_wait on the +16 word; 1→0 transition notifies).
4. **Mark** from registered roots (handle-table in-registry slots). Follows payload ref words.
5. **Sweep** unmarked blocks → size-class free lists. Release handle slots with generation bump.
6. **Release**: wake guest threads (release_fn), then epoch→even + host_park bump + word_notify_all.

### Safe Points (Complete Set)

1. **Allocation** — epoch odd → park on host_park, retry
2. **Host invoke frames** — park at the GC barrier (native condvar / web bounded-poll)
3. **Managed guest threads** — quiesce handshake parks the guest on its own word

### Guest Loop Protocol + Self-Quiesce Rule

A `threads: managed` guest thread runs:

```
check request flag → park on OWN word → release → alloc → ...
```

- `quiesce_fn` (host) polls `parked_seq` — NOT the park word
- Acceptance set: parked for THIS request ∨ asleep after previous release ∨ inside alloc (safe point)
- **Self-quiesce rule**: if alloc triggers synchronous auto-collect, `quiesce_fn` must return OK immediately (the thread is inside alloc, which is a safe point by construction — deadlocking on itself is the alternative)

- **File**: `core/sources/memory/shim.hpp` (lines 100–141)

---

## Dual-Shim Architecture

The memory algorithm code (`memory_manager.cpp`) is platform-neutral — it never includes `pthread.h`, `emscripten.h`, or WAMR headers. All platform primitives reach it through the `MobagenMemoryShim` vtable:

- **File**: `core/sources/memory/shim.hpp`

### Native Shim (WAMR-heap + pthread)

- **File**: `core/sources/memory/native_shim.hpp`, `native_shim.cpp`
- **Target**: `mobagen::memory_native_shim` (native only, not EMSCRIPTEN)
- Two modes: **standalone** (owns a 256 KiB `std::aligned_alloc` region, grows on demand) and **foreign** (borrows the WAMR shared-heap region, never frees)
- Atomic ops: `__atomic_load_n` / `__atomic_store_n` / `__atomic_compare_exchange_n` with `__ATOMIC_SEQ_CST`
- Wait/notify: 16 pthread mutex/condvar buckets keyed `(word_offset >> 2) & 15`
- `word_wait` re-checks the word under the bucket mutex before sleeping (predicate: value != current)

### Web Shim (SAB + Atomics)

- **File**: `core/sources/memory/web_shim.hpp`, `web_shim.cpp`
- **Target**: `mobagen::memory_web_shim` (EMSCRIPTEN only, both web variants)
- The region lives IN THE EMSCRIPTEN HEAP (not a fresh SAB) — wasm-ld needs a linear-memory pointer
- In the **shared** variant (heap = SharedArrayBuffer): `Atomics.*` on the live `HEAP32` view are real cross-agent atomics; `Atomics.wait/notify` work on worker agents
- In the **isolated** variant (heap = ArrayBuffer): atomics degrade to volatile u32, wait/notify degrade to no-op-yield, one loud degradation trace

### Threading Rule (Web)

Firefox and Safari **forbid** `Atomics.wait` on the main thread (throws even in cross-origin-isolated contexts). Blocking waits run on **worker agents only**. The host main thread participates in stop-the-world via bounded polling (~1 ms paced poll, hard timeout cap). Enforced by agent classification in JS + capability try/catch wrapping every `Atomics.wait`.

---

## Guest Toolchain Requirements

### emcc (Emscripten) Target

```bash
emcc -O2 -matomics -mbulk-memory \
     -sUSE_PTHREADS=0 -sSHARED_MEMORY=0 \
     -sSTANDALONE_WASM=1 -sALLOW_MEMORY_GROWTH=0 \
     -Wl,--no-entry -Wl,--export-memory \
     -Wl,--export=mobagen_module_entry_v1 \
     -Wl,--export=mobagen_module_exports_v1 \
     -o plugin.wasm
```

**Critical**: `-sUSE_PTHREADS=0 -sSHARED_MEMORY=0` **must appear AFTER** any inherited flags that set them to 1 (the shared web tree appends `-sUSE_PTHREADS=1 -sSHARED_MEMORY=1` to every target; emcc last-flag-wins). Without this override, the guest imports the emscripten pthread ABI (forbidden on WAMR) and emits PASSIVE data segments (the extraction tool cannot resolve annotation tables from passive segments).

- `-matomics -mbulk-memory` emit atomic instructions; emcc does NOT emit a `target_features` section, so shared-memory capability is stamped via `manifest --shared-memory` at build time.
- Reference: `core/sources/plugins/browser/CMakeLists.txt` (lines 28–45), T18 learnings.

### wasi-sdk (Clang/WASI) Target

```bash
external/wasi-sdk-34/bin/clang \
  --target=wasm32-wasip1 \
  --sysroot=external/wasi-sdk-34/share/wasi-sysroot \
  -O2 -nostdlib -matomics -mbulk-memory \
  -Wl,--no-entry -Wl,--export-memory \
  -Wl,--export=mobagen_module_entry_v1 \
  -Wl,--export=mobagen_module_exports_v1 \
  -o plugin.wasm
```

- `-nostdlib` yields a ZERO-import self-contained guest that WAMR loads as-is
- `-matomics` adds `+atomics` to the `target_features` custom section (the extraction tool auto-detects this)
- `--target=wasm32-wasip1` (not `wasm32-wasi` — the old spelling is deprecated)
- wasi-sdk is pinned at **34.0** (`scripts/toolchains.py`)
- Reference: T18 learnings, `core/sources/modules/module_manifest.cpp`

### Native Shim Standalone (for benchmarking)

```bash
# Links mobagen::memory_core + mobagen::memory_native_shim
# Uses make_native_shim() — owns a 256 KiB region, no WAMR needed
```

- Target: `mobagen::memory_native_shim` (native only, `core/sources/memory/CMakeLists.txt` line 18)
- Factory: `make_native_shim()` returns a `unique_ptr<NativeShim>` with standalone ownership

---

## Packaging (v2 Layout)

A `.plugin` package is a directory containing:

| File | Required | Description |
|---|---|---|
| `plugin.wasm` | Yes | The wasm binary |
| `plugin.aot` | No | AOT-compiled binary (desktop/Android only) |
| `module.manifest` | Yes (v2) | The schema-v2 manifest |

### AOT Stage (Desktop/Android)

- **Tool**: `wamrc` (WAMR 2.4.5), invoked via `mobagen_add_module_aot_stage()` in `cmake/mobagen_module_aot.cmake`
- **Flags per arch**:
  - x86_64: `--size-level=1` (medium code model)
  - aarch64: `--size-level=3` (small code model; wamrc's aarch64 default)
- iOS: AOT is always disabled (compile-time guard)
- Cross aarch64 output is suffixed `plugin.aarch64.aot` to avoid collision

### Size Budget

All guest binaries must fit within **8 MiB** (asserted by `quickjs_size_check.cmake` and build-system size gates).

---

## Hot-Path Rule

**Coarse calls only.** Per-frame, per-tick work never crosses the module boundary. The annotation table defines long-lived capability calls (init, configure, start, stop, process). Frame-rate-sensitive work (rendering, physics integration, input) stays on the host side. This rule is the primary reason the wasm ABI is coarse-grained — crossing the wasm<->host boundary carries non-trivial cost (marshaling, mirror flush on the browser backend, potential GC safe-point contention).

- Reference: `sdk/include/mobagen/module/module_abi.h` (line 9): *"coarse calls only. Per-frame hot-path work never crosses the module boundary."*

---

## File Reference

| File | Purpose |
|---|---|
| `sdk/include/mobagen/module/module_abi.h` | ABI structs, signature encoding, annotation macros, publish bridge |
| `core/sources/memory/shim.hpp` | Memory shim vtable contract, safe points, control block layout |
| `core/sources/memory/object_header.h` | Object format v1, handle encoding, size classes |
| `core/sources/memory/memory_manager.hpp` | MemoryManager API: alloc, collect, roots, guest threads |
| `core/sources/memory/native_shim.hpp` | Native shim (standalone / WAMR foreign) |
| `core/sources/memory/web_shim.hpp` | Web shim (SAB + Atomics / isolated fallback) |
| `core/sources/modules/module_manifest.hpp` | Manifest schema v2 structs, export marshaling |
| `core/sources/modules/module_manifest.cpp` | Manifest parser + serializer |
| `cmake/mobagen_module_aot.cmake` | AOT stage helper (wamrc invocation) |
| `scripts/toolchains.py` | Toolchain fetch (emcc, wasi-sdk, emception) |
