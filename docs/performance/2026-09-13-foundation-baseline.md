# Mobagen foundation performance baseline

- Date: 2026-09-13
- Baseline schema: `mobagen.baseline.v1`
- Benchmark schema: `mobagen.foundation-benchmark.v1`

## Purpose

This is the pre-module-kernel CPU baseline for Mobagen's foundation layer. It establishes a repeatable reference for startup construction, ECS iteration, job dispatch, render-command extraction, and a representative foundation frame. It is calibration evidence, not yet a measurement of modular-runtime overhead and not a GPU benchmark.

The architecture work started from remote `master` commit `4dd5d889b666463ae6c36c4b4245a9c8eb6e1208` (`v1.23.2`). The captured executable and capture tooling are from `feature/modular-runtime` commit `8c70a42f6c4b262aff0702d02cdd9962aa990faa`; the evidence reports `dirty: false` at the start of capture.

Machine-readable evidence: [2026-09-13-windows-msvc-release.json](baselines/2026-09-13-windows-msvc-release.json)

## Host and toolchain

| Field | Captured value |
| --- | --- |
| OS | Windows 10 |
| Architecture | AMD64 |
| Processor | Intel64 Family 6 Model 85 Stepping 7, GenuineIntel |
| Configuration | Release |
| Compiler | MSVC 19.51.36252.0 |
| CMake | 4.3.3 |
| Python | 3.10.11 |
| Warm-up iterations | 5 |
| Retained samples | 30 |

## Results

Times are wall-clock CPU durations from `std::chrono::steady_clock`. The render bridge measurement builds CPU-side draw commands; it does not submit work to WebGPU.

| Workload | Median | p95 | Work performed per sample |
| --- | ---: | ---: | --- |
| `startup.foundation` | 0.1218 ms | 0.1308 ms | Construct a foundation world, inline scheduler, render bridge, 1,024 entities, and 64 renderables |
| `ecs.serial_update` | 0.7817 ms | 1.4942 ms | Update Position from Velocity across 200,000 entities |
| `jobs.parallel_for` | 1.9989 ms | 2.1162 ms | Update 200,000 disjoint integers with grain size 1,024, then wait for completion |
| `render.bridge_build` | 0.1185 ms | 0.1771 ms | Extract 10,000 volume draw commands from the 200,000-entity world |
| `frame.foundation` | 0.8690 ms | 1.2846 ms | Perform one serial ECS position update followed by one render-bridge build |

Raw nanosecond samples are retained in the machine-readable evidence so later comparisons can use distributions rather than rounded table values.

## Build-graph baseline and defects found

Before the foundation seam existed, `MOBAGEN_BUILD_RUNTIME=OFF` was ignored and configuration still entered the complete graphics/UI dependency graph. The SDL3 subconfigure alone reported 452 seconds, and the diagnostic full configure was stopped after more than nine minutes once the coupling defect had been demonstrated. `CoreTests` pulled Dawn/Tint, SDL3, ImGui, and RmlUi even for ECS and scheduler tests.

The first isolated MSVC build also exposed an ownership defect at `core/sources/camera/camera.hpp:248`: the header used `std::max` without including `<algorithm>`. Before coverage flags were scoped by compiler, MSVC ignored the GCC flags `-O0`, `-g`, `-fprofile-arcs`, and `-ftest-coverage`, including the corresponding link flags.

After the seam and header fix, the first uncached foundation-only configure completed in 13.4 seconds. Subsequent cached configuration took approximately one second. The Debug foundation and benchmark suite passed 27 of 27 tests.

## Reproduction

From the feature worktree root:

```powershell
cmake -S . -B build-foundation `
  -DMOBAGEN_BUILD_RUNTIME=OFF `
  -DBUILD_EXAMPLES=OFF `
  -DMOBAGEN_BUILD_BENCHMARKS=ON `
  -DENABLE_DOCUMENTATION=OFF `
  -DENABLE_TEST_COVERAGE=ON `
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-foundation --config Debug `
  --target FoundationTests MobagenFoundationBenchmark --parallel 2

ctest --test-dir build-foundation -C Debug `
  -L "foundation|benchmark" --output-on-failure

cmake --build build-foundation --config Release `
  --target MobagenFoundationBenchmark --parallel 2

python scripts/baseline.py `
  --binary build-foundation/bin/Release/MobagenFoundationBenchmark.exe `
  --output docs/performance/baselines/2026-09-13-windows-msvc-release.json `
  --configuration Release `
  --compiler "MSVC 19.51.36252.0" `
  --warmup 5 `
  --samples 30
```

On MSVC, `ENABLE_TEST_COVERAGE=ON` keeps the tests enabled but deliberately skips unsupported GCC/Clang coverage instrumentation.

## Limitations and allocation notes

- This is one capture from one Windows host. CPU frequency, background load, affinity, and thermal state were not pinned. CI should detect broad regressions; controlled performance hosts are still required for a hard 1% product gate.
- The dependency/build failures were observed in Debug-oriented diagnostic builds, while the timings above are from Release.
- The harness does not time WebGPU device creation, queue submission, shader compilation, presentation, or GPU execution.
- `RenderBridge` retains vector capacity after warm-up, so its repeated build does not intentionally grow the command buffer. The ECS fixture is also preallocated before measurement.
- `Scheduler::parallel_for` creates coroutine frames on the submitting thread and currently returns completed frames to the worker thread's `thread_local` pool. Cross-thread completion therefore prevents reliable reuse by the submitter and can cause per-batch allocations after warm-up. The baseline includes this current cost; allocator instrumentation and ownership correction belong in foundation hardening.
- `startup.foundation` intentionally includes construction and allocation. It must not be used as evidence for the zero-allocation steady-state target.
