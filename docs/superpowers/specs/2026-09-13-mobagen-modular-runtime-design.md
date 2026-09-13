# Mobagen Modular Runtime Design

**Status:** Accepted
**Date:** 2026-09-13
**Branch:** `feature/modular-runtime`
**Baseline:** `4dd5d889b666463ae6c36c4b4245a9c8eb6e1208` (`v1.23.2`)

## Context

Mobagen is intended to be both internally modular and externally extensible. A developer must be
able to assemble a minimal runtime, a game, an editor, or a domain-specific application from the
same set of capabilities. Third parties must be able to install `.plugin` packages comparable in
role to Maya plugins, Blender add-ons, Unreal plugins, and Unity packages.

The current tree already contains useful leaf libraries for ECS, jobs, messaging, scene, rendering,
resources, application hosting, and UI. However, the umbrella `core` target couples foundational
tests to SDL, Dawn, ImGui, and RmlUi. Entity and resource identity contracts are not consistently
preserved, several binary readers trust their inputs, and the current asset workflow is not part of
the clean web build. The modular runtime must therefore be introduced incrementally, on top of a
hardened foundation, without a big-bang rewrite.

## Goals

1. Compose Mobagen products through a small, human-readable `mobagen.yaml` descriptor.
2. Resolve that intent deterministically into a generated `mobagen.lock`.
3. Use the same capability model for built-in modules and installable `.plugin` packages.
4. Preserve near-monolithic performance for release builds through generated static composition,
   whole-program optimization, and coarse-grained dispatch.
5. Support native plugins through a versioned C ABI and portable plugins through a bounded
   WebAssembly ABI.
6. Provide a reproducible asset pipeline with content-addressed artifacts and plugin-defined
   importers, processors, cookers, loaders, inspectors, and thumbnail providers.
7. Keep every existing application runnable throughout the migration.
8. Make module resolution, plugin activation, asset production, and failure recovery observable and
   testable.

## Non-goals for the First Release

- Arbitrary live replacement of the GPU device, renderer, scheduler, allocator, or other global
  runtime infrastructure.
- A public plugin marketplace, payment system, or automatic internet installation.
- One universal `.plugin` containing binaries for every target. Version one packages are
  target-specific.
- A stable C++ binary ABI across compilers, standard libraries, or build modes.
- Executing clinical DICOM data in shared or remote caches without an explicit data policy.
- Dynamic linkage for every subsystem. Static composition remains the preferred shipping path.

## Terminology

### Module

A module is a logical unit with one responsibility, declared dependencies, provided capabilities,
configuration, lifecycle, and tests. A module may be built into the executable or supplied by a
plugin.

### Capability

A capability is a versioned public contract, such as `render.backend.v1`, `asset.importer.v1`, or
`editor.panel.v1`. Capabilities describe behavior; they do not name a concrete implementation.

### Provider

A provider implements one or more capabilities. `mobagen.render.webgpu`, for example, may provide
`render.backend.v1`.

### Plugin

A plugin is a single target-specific `.plugin` file containing a minimal manifest, one executable
payload, and optional assets. A plugin can contribute one or more providers.

### Product Descriptor and Lockfile

`mobagen.yaml` states what a project wants. `mobagen.lock` records the exact providers, versions,
hashes, dependency graph, linkage modes, SDK version, and ABI versions selected by the resolver.
Both files are committed. The descriptor is edited by a developer; the lockfile is generated.

## Architecture Overview

```text
mobagen.yaml       plugins/*.plugin        built-in providers
      |                    |                       |
      +--------------------+-----------------------+
                           |
                    discovery and validation
                           |
                    deterministic resolver
                           |
                      mobagen.lock
                           |
              generated and/or dynamic adapters
                           |
                   frozen execution graph
                           |
          ECS / jobs / assets / renderer / editor
```

The descriptor, manifests, names, semantic versions, and dependency graph belong to the control
plane. They are processed during resolution and startup. The frame loop and other hot paths belong
to the data plane and receive already-resolved function tables, typed handles, spans, command
buffers, and direct references.

## Composition Files

### `mobagen.yaml`

The project descriptor uses a strict YAML 1.2 subset:

```yaml
schema: 1
name: dicom-viewer

modules:
  render:
    use: default
  window:
    use: default
  ui:
    use: imgui
  assets:
    use: default
  volume-importer:
    use: dicom

plugins:
  - ./plugins/custom-transfer.plugin

profiles:
  editor:
    linkage: dynamic
  web:
    linkage: static
  release:
    linkage: static
    editor: false
```

The parser rejects duplicate keys, custom executable tags, unknown fields, invalid scalar types,
and unsupported schema versions. Validation errors include the source path, line, column, field,
and expected contract. Environment interpolation is not part of schema version 1 because it would
make resolution dependent on undeclared machine state.

`default` is a request for the official provider selected for the active target and profile. The
resolver records the concrete choice in the lockfile, so a later default change cannot silently
change an existing project.

### `mobagen.lock`

The lockfile is deterministic generated YAML without a filename extension beyond `.lock`:

```yaml
schema: 1
sdk: 1.0.0
profile: web

resolved:
  render:
    capability: render.backend.v1
    provider: mobagen.render.webgpu
    version: 1.4.2
    linkage: static

plugins:
  customer.custom-transfer:
    version: 2.0.1
    hash: sha256:abc123
```

Stable ordering and normalized scalar formatting make identical resolutions byte-for-byte equal.
The runtime never reparses YAML in a frame. A release build consumes generated registry data and
can omit the YAML parser entirely.

## Module Contract and Resolution

Each module descriptor contains a stable provider ID, semantic version, provided capabilities,
required capabilities, optional capabilities, conflicts, supported targets, supported linkage
modes, reload policy, configuration schema, and permissions.

Resolution follows this order:

1. Discover built-in descriptors and project-local `.plugin` manifests.
2. Validate descriptor schemas, target compatibility, hashes, SDK ranges, and ABI ranges.
3. Expand requested module aliases into capability requirements.
4. Select explicit providers before defaults.
5. Resolve remaining defaults using the target/profile default table.
6. Validate required capabilities, conflicts, cycles, cardinality, and permissions.
7. Produce a stable topological lifecycle order.
8. Write the complete lockfile or leave the previous lockfile unchanged on failure.

Ties are errors unless an explicit priority is part of the official target/profile default table.
Filesystem enumeration order must never affect the result. A diagnostic explains every selected
provider and the chain that required it.

Modules receive a `ModuleContext` containing only declared capabilities. A process-wide service
locator is prohibited. Built-in modules and plugin providers share the same semantic contract, but
built-ins may use generated typed adapters that eliminate the dynamic ABI.

## Lifecycle

The lifecycle is:

```text
discover -> validate -> resolve -> load -> register -> configure -> start -> freeze
                                                                    |
                                                                  execute
                                                                    |
                                                    quiesce -> stop -> unload
```

Registration and activation are transactional. A provider is invisible to the live graph until
loading, ABI negotiation, dependency validation, and configuration all succeed. Failure discards
staged registrations and preserves the previous graph.

After `freeze`, capability lookup by string is forbidden in hot paths. Reloadable plugins must stop
new work, wait for outstanding calls, optionally serialize versioned state, validate the new
generation, and swap immutable dispatch tables at a safe point. Version one requires a controlled
restart for critical infrastructure modules.

## Linkage and Performance Tiers

The same capability model supports four physical execution tiers:

| Tier | Intended use | Mechanism |
| --- | --- | --- |
| 0 | Renderer, ECS, jobs, physics, shipping runtime | Generated static composition and LTO |
| 1 | Trusted native extensions | Dynamic library and versioned C function table |
| 2 | Portable or untrusted extensions | Bounded WebAssembly imports, exports, and linear memory |
| 3 | Editor automation and heavy tools | Script or isolated worker process |

Shipping profiles promote selected providers to static linkage. Development/editor profiles may
load native or WebAssembly plugins. Performance comes from coarse-grained operations: one dispatch
per system, chunk, render pass, or command batch rather than one dispatch per entity, voxel,
triangle, or property.

Hot-path requirements are:

- no descriptor, manifest, semantic-version, hash-map, or string lookup;
- no allocation after warm-up unless explicitly budgeted;
- contiguous data and batch-oriented contracts;
- immutable dispatch tables while work is executing;
- no JavaScript/WebAssembly boundary crossing inside per-entity or per-draw loops;
- no plugin-owned C++ object crossing a binary boundary.

The initial performance target is no more than 1% median CPU frame overhead relative to the
equivalent monolithic baseline. Benchmarks use repeated samples and publish distributions. A 3%
regression threshold is used as the automated gate until the benchmark environment is stable
enough to enforce the 1% target directly.

## `.plugin` Package and Native ABI

The only public installable extension is `.plugin`. It is a standard ZIP-compatible container with
the following logical contents:

```text
custom-renderer.plugin
  plugin.json
  module.dll | module.so | module.dylib | module.wasm
  assets/
```

`plugin.json` contains the provider ID, version, SDK range, ABI version, target, provided and
required capabilities, permissions, reload policy, payload path, and payload hash. The project
lockfile records the hash of the complete `.plugin` file.

Native payloads export one required C symbol:

```c
MobagenResult mobagen_plugin_query(
    const MobagenHostApi* host,
    MobagenPluginApi* plugin);
```

All public structures begin with `abi_version` and `struct_size`. The ABI uses fixed-width integers,
explicit enum storage, handles, spans, opaque instance pointers, host-owned allocators, and function
tables. STL containers, C++ exceptions, RTTI, compiler-specific classes, and ownership transfer of
raw pointers are forbidden across the boundary. C++ SDK wrappers may provide ergonomics without
changing the binary contract.

Native plugins are trusted code. The loader stages extraction in a private cache, prevents path
traversal, validates sizes and hashes before loading, negotiates the ABI, and commits registration
only after validation succeeds.

## WebAssembly Plugins and Web Shipping

Web shipping builds use static composition by default so whole-program optimization, dead-code
elimination, and a minimal download remain possible. The descriptor still selects providers; the
build generator changes their physical linkage.

Development/editor Web builds may use two plugin modes:

1. Toolchain-coupled Emscripten side modules for trusted providers built with the exact Mobagen SDK
   and compatible runtime settings.
2. Portable WebAssembly plugins using a Mobagen-defined ABI with bounded imports, numeric handles,
   linear-memory spans, and batch command buffers.

The portable ABI exposes only permissions declared by the plugin. It does not expose the complete
Emscripten runtime or raw WebGPU objects. Dynamic linking and Web threads are separate capabilities;
version one does not combine runtime side-module loading with the threaded scheduler. Web thread
support requires deployment headers and an explicit memory policy rather than a build flag alone.

## Asset Manager

Authoring formats never enter the frame loop. The pipeline is:

```text
source -> fingerprint -> importer -> intermediate -> processors -> platform cooker
       -> content-addressed cache -> runtime artifact
```

The asset model contains:

- immutable source identity and provenance;
- a recipe with importer/provider version, options, dependencies, and target;
- a logical `AssetId` used by projects and scenes;
- a runtime `{index, generation}` handle;
- an artifact key derived from source content, recipe, provider versions, and target;
- a dependency DAG for precise invalidation;
- atomic publication into a content-addressed cache.

Plugins may provide `asset.importer.v1`, `asset.processor.v1`, `asset.cooker.v1`,
`asset.loader.v1`, `asset.thumbnail-provider.v1`, and `asset.inspector.v1`. Runtime-critical loaders
are static or native. Heavy and failure-prone importers may execute in an isolated worker process.

The initial CLI surface is:

```text
mobagen assets scan
mobagen assets import
mobagen assets cook --target <target>
mobagen assets verify
mobagen assets explain <asset-id>
mobagen assets clean
mobagen assets doctor
```

The current asset manifest and hashes are migration inputs. Clean Web CI must run asset cooking.
Missing required artifacts fail the build. A fallback is permitted only when explicitly declared in
`mobagen.yaml`, and runtime diagnostics identify the selected fallback.

DICOM import groups files by series identity, validates every slice, enforces dimension and memory
limits, records orientation and modality metadata, and preserves provenance. Sources and artifacts
are classified as synthetic, public, anonymized, or restricted. Restricted data is excluded from
Git and shared caches unless an explicit policy authorizes publication.

## User Experience

A project has a small conventional layout:

```text
my-project/
  mobagen.yaml
  mobagen.lock
  plugins/
    custom-transfer.plugin
  assets/
  src/
```

The initial commands are:

```text
mobagen plugin install <file.plugin>
mobagen plugin remove <provider-id>
mobagen plugin list
mobagen plugin new <name>
mobagen plugin pack
mobagen plugin test
mobagen resolve
mobagen run --profile <profile>
mobagen build --profile <profile>
```

The editor Module Manager displays required capabilities, active providers, installed alternatives,
dependency and conflict diagnostics, linkage per profile, and the reason for each resolution. It
edits `mobagen.yaml`, invokes the same resolver used by the CLI, and presents the generated lockfile;
it does not maintain a second configuration model.

Initial editor extension points are render backends and passes, asset importers and inspectors,
editor panels and commands, component inspectors, scene serializers, and game systems.

## Foundation Hardening Requirements

The module system relies on contracts that must be corrected before broad plugin exposure:

- every ECS operation validates entity generation and safely represents missing or duplicate
  components;
- resource handles preserve index and generation through scenes, render commands, serialization,
  and lookup;
- scene, transport, `.plugin`, and `.mvol` readers enforce bounds, count limits, type constraints,
  and overflow-safe allocation;
- transforms detect parent cycles and reject stale parents;
- jobs, reactive state, events, ECS, and GPU state have explicit thread ownership;
- worker identity is associated with a scheduler instance;
- shutdown drains or cancels outstanding work deterministically;
- WebGPU allocation and device-loss paths cannot submit null resources;
- plugin calls cannot outlive their loaded generation.

Each defect is introduced with a failing regression test before its implementation change.

## Error Handling and Observability

Public operations return structured error codes plus diagnostic records. Diagnostics include phase,
provider, capability, source path when applicable, causal chain, and remediation. Plugin code writes
through host-provided logging and tracing APIs so logs remain valid after unload.

Resolution, asset publication, plugin installation, activation, lockfile replacement, and hot reload
are transactional. Temporary files use same-filesystem atomic replacement. Failed operations do not
leave partially installed packages, partially published artifacts, or a partially mutated live
graph.

## Test and Verification Strategy

The required layers are:

1. Unit tests for IDs, versions, manifests, strict YAML, graph resolution, lifecycle, handles,
   serializers, cache keys, and ABI negotiation.
2. Adversarial tests for malformed files, overflow, path traversal, dependency cycles, stale
   handles, duplicate providers, missing capabilities, invalid permissions, and reload races.
3. Integration tests for static built-ins, native `.plugin` loading, portable WebAssembly loading,
   transactional rollback, asset cooking, and lockfile reproducibility.
4. End-to-end compositions for a headless runtime and an SDL3/WebGPU application using the same
   module kernel.
5. Platform verification on Windows, Linux, macOS, and Web when the feature applies.
6. ASan/UBSan jobs, race-oriented tests for threaded systems, and fuzz targets for every untrusted
   binary boundary.
7. Benchmarks comparing direct, generated-static, native-function-table, and WebAssembly dispatch,
   plus ECS, scheduler, asset load, and render submission baselines.

The existing misleading ECS performance test is replaced with a correctness test and a separate
benchmark executable. Coverage instruments the libraries under test, not only the test executable.
The foundation test target must not build Dawn or UI dependencies.

## Incremental Delivery

### Milestone 0: Isolated Baseline

Create the dedicated worktree and branch, protect existing generated assets, restore asset ignore
rules, verify the clean build/test baseline, and record compile and performance measurements.

### Milestone 1: Foundation Hardening

Repair ECS, resource identity, bounded readers, transform validation, thread ownership, scheduler
shutdown, and WebGPU error paths through independent regression-tested commits.

### Milestone 2: Module Kernel

Implement strict `mobagen.yaml`, capability/provider descriptors, deterministic resolution,
`mobagen.lock`, lifecycle, frozen dispatch, and two end-to-end built-in compositions.

### Milestone 3: Asset Manager

Implement content-addressed artifacts, recipes, dependency invalidation, runtime handles, CLI, Web
cooking, and migration of the current asset script and DICOM path.

### Milestone 4: Native `.plugin`

Implement packaging, installation, the C ABI, native loader, transactional activation, SDK helpers,
and a small reference provider before attempting a complete renderer replacement.

### Milestone 5: Web Composition

Implement generated static shipping composition, portable WebAssembly loading, bounded host imports,
batch command exchange, browser tests, and an explicit Web thread/deployment policy.

### Milestone 6: Module Manager

Implement the editor UI over the same descriptor, resolver, lockfile, installation, diagnostics, and
safe reload services used by the CLI.

### Milestone 7: Product Migration

Separate foundation, platform, graphics, and UI targets; migrate DICOM, Life, Flocking, and demos;
split the DICOM monolith; correct release metadata; replace stale current-state documentation; and
ship minimal-engine, editor, and custom-application examples.

## Planning Decomposition

This document is the umbrella architecture, not a single implementation batch. Implementation is
split into one independently reviewable plan per milestone. A later plan may depend only on public
interfaces and verified deliverables committed by earlier plans. No milestone plan may silently
expand the stable ABI or capability surface; such a change requires an explicit amendment to this
design.

The first implementation plan covers only Milestone 0 and the foundation seams needed to make its
baseline trustworthy. Milestones 1 through 7 receive separate plans before their code work begins,
keeping tests, review boundaries, commits, and remote progress legible to the client.

## Delivery Discipline

All work occurs in the dedicated worktree on `feature/modular-runtime`. Neither `master` nor
`develop` is used as a working branch. Existing untracked assets in the original checkout remain
untouched.

Each independently complete task ends with its focused tests, a small coherent commit, and an
immediate push to `origin/feature/modular-runtime` so the client can observe progress. Commits do not
mix foundation fixes, module kernel work, assets, plugin loading, Web composition, or editor UI.
History is never force-pushed. If a task cannot pass its relevant verification, it is not presented
or pushed as complete; the failure is reported with evidence.

## Consequences

Mobagen gains a single composition language and capability model without imposing the same linkage
cost on every use case. Shipping builds remain aggressively static and optimizable, while editor
builds can load installable plugins. The cost is a larger initial investment in public contracts,
deterministic tooling, security boundaries, tests, and migration adapters. These contracts become
long-lived API surface, so the first stable capability and ABI versions must remain deliberately
small.
