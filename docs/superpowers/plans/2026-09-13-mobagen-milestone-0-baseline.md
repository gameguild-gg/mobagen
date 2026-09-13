# Mobagen Milestone 0 Baseline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish a protected, reproducible, cross-platform foundation baseline that builds and tests without pulling the graphics/editor stack and records trustworthy ECS, scheduler, render-bridge, and frame-loop measurements.

**Architecture:** Add a foundation-only CMake composition beside the backward-compatible full runtime, split fast foundation tests from graphics-host integration tests, and introduce a dependency-free benchmark runner that emits machine-readable evidence. Keep generated medical assets outside Git while leaving their manifest and documentation reviewable.

**Tech Stack:** CMake 3.16.3+, C++20/23, doctest, Python 3 standard library, GitHub Actions, MSVC/GCC/Apple Clang.

**Spec:** `docs/superpowers/specs/2026-09-13-mobagen-modular-runtime-design.md`

## Global Constraints

- Work only in the dedicated worktree on `feature/modular-runtime`; never develop on `master` or `develop`.
- Preserve all untracked assets in the original checkout.
- End every independently complete task with focused verification, a small coherent commit, and an immediate push to `origin/feature/modular-runtime`.
- Do not force-push.
- Keep `MOBAGEN_BUILD_RUNTIME=ON` as the default so existing consumers retain the complete SDL3, Dawn, ImGui, and RmlUi composition.
- The foundation test target must not build SDL3, Dawn, ImGui, or RmlUi.
- Coverage flags may be emitted only for compilers that support them.
- Replace timing assertions in correctness tests with deterministic behavior assertions.
- Benchmark output must contain raw samples plus median and p95 values; do not reduce evidence to one elapsed-time number.
- The future modular-runtime target remains no more than 1% median CPU-frame overhead over this baseline; CI initially gates statistically significant regressions above 3%.
- Do not introduce the module kernel, plugin ABI, Asset Manager rewrite, or editor Module Manager in this milestone.

## File Map

- `.gitignore`: generated/downloaded asset boundaries.
- `test/python/test_asset_ignore_policy.py`: executable Git ignore-policy contract.
- `CMakeLists.txt`: top-level runtime, test, benchmark, and dependency-composition switches.
- `core/CMakeLists.txt`: foundation targets always available; platform/graphics/editor targets conditional.
- `core/sources/camera/camera.hpp`: self-contained standard-library dependency for `std::max`.
- `test/CMakeLists.txt`: separate foundation and app-host executables plus compatibility aggregate target.
- `cmake/coverage.cmake`: compiler-aware coverage instrumentation helper.
- `test/ecs_tests.cpp`: deterministic ECS range behavior only.
- `benchmarks/CMakeLists.txt`: foundation benchmark target.
- `benchmarks/benchmark_runner.hpp`: warm-up, sampling, median, p95, and JSON serialization.
- `benchmarks/foundation_baseline.cpp`: ECS, scheduler, render-bridge, startup, and representative frame workloads.
- `scripts/baseline.py`: invokes the benchmark and adds Git/platform/compiler provenance.
- `test/python/test_baseline_script.py`: validates capture failure and success behavior.
- `docs/performance/baselines/2026-09-13-windows-msvc-release.json`: first machine-readable baseline captured on the current host.
- `docs/performance/2026-09-13-foundation-baseline.md`: commands, environment, results, and known pre-existing failures.
- `.github/workflows/test.yml`: foundation matrix on Windows, Linux, and macOS plus bounded runtime integration.

---

### Task 1: Protect generated and restricted medical assets

**Files:**

- Modify: `.gitignore`
- Create: `test/python/test_asset_ignore_policy.py`

**Interfaces:**

- Consumes: the existing paths in `apps/dicom_viewer/assets/assets.json`.
- Produces: a Git policy where `assets.json` and `README.md` remain reviewable while cache, raw volume, cooked volume, and DICOM study contents remain untracked.

- [ ] **Step 1: Write the failing ignore-policy test**

```python
from __future__ import annotations

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def is_ignored(path: str) -> bool:
    result = subprocess.run(
        ["git", "check-ignore", "--quiet", "--no-index", path],
        cwd=ROOT,
        check=False,
    )
    return result.returncode == 0


class AssetIgnorePolicyTest(unittest.TestCase):
    def test_generated_and_restricted_assets_are_ignored(self) -> None:
        for path in (
            "apps/dicom_viewer/assets/cache/head.zip",
            "apps/dicom_viewer/assets/head256x256x109.raw",
            "apps/dicom_viewer/assets/volume.raw",
            "apps/dicom_viewer/assets/volume.mvol",
            "apps/dicom_viewer/assets/dicom/study/series/slice.dcm",
        ):
            with self.subTest(path=path):
                self.assertTrue(is_ignored(path), path)

    def test_policy_files_are_not_ignored(self) -> None:
        for path in (
            "apps/dicom_viewer/assets/assets.json",
            "apps/dicom_viewer/assets/README.md",
        ):
            with self.subTest(path=path):
                self.assertFalse(is_ignored(path), path)
```

- [ ] **Step 2: Run the policy test and confirm the current gap**

Run: `python -m unittest discover -s test/python -p "test_asset_ignore_policy.py" -v`

Expected: FAIL for every generated or restricted path because the current `.gitignore` has no DICOM asset rules.

- [ ] **Step 3: Add the narrow ignore rules**

Append this exact block to `.gitignore`:

```gitignore
# DICOM viewer asset-manager outputs and restricted source studies
apps/dicom_viewer/assets/cache/
apps/dicom_viewer/assets/dicom/
apps/dicom_viewer/assets/*.raw
apps/dicom_viewer/assets/*.mvol
!apps/dicom_viewer/assets/assets.json
!apps/dicom_viewer/assets/README.md
```

- [ ] **Step 4: Verify policy and preservation**

Run: `python -m unittest discover -s test/python -p "test_asset_ignore_policy.py" -v`

Expected: 2 tests PASS.

Run in the original checkout: `git status --short -- apps/dicom_viewer/assets`

Expected: the files remain physically present; the command no longer lists generated outputs after the branch is eventually merged. Before that merge, compare the previously captured 100-file/15,762,195-byte inventory and confirm no file was removed or changed.

- [ ] **Step 5: Commit and synchronize**

```bash
git add .gitignore test/python/test_asset_ignore_policy.py
git commit -m "chore: protect generated medical assets"
git push origin feature/modular-runtime
```

### Task 2: Introduce a foundation-only build and test seam

**Files:**

- Modify: `CMakeLists.txt`
- Modify: `core/CMakeLists.txt`
- Modify: `core/sources/camera/camera.hpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**

- Consumes: existing leaf targets `mobagen::core_ecs`, `core_jobs`, `core_reactive`, `core_messaging`, `core_scene`, `core_camera`, `core_input`, `core_render`, and `core_resource`.
- Produces: cache option `MOBAGEN_BUILD_RUNTIME:BOOL`, executable `FoundationTests`, optional executable `AppHostTests`, and compatibility aggregate target `CoreTests`.

- [ ] **Step 1: Reproduce the missing seam**

Run:

```bash
cmake -S . -B build-foundation -DMOBAGEN_BUILD_RUNTIME=OFF -DBUILD_EXAMPLES=OFF -DENABLE_DOCUMENTATION=OFF -DENABLE_TEST_COVERAGE=ON
cmake --build build-foundation --config Debug --target FoundationTests --parallel 2
```

Expected: configure warns that `MOBAGEN_BUILD_RUNTIME` is unused and build fails because `FoundationTests` does not exist.

- [ ] **Step 2: Make runtime dependency composition conditional**

Define `MOBAGEN_BUILD_RUNTIME` before `include(external/external.cmake)` in the root file and use it around the heavy dependencies and license generation:

```cmake
option(MOBAGEN_BUILD_RUNTIME "Build SDL3, Dawn, ImGui, RmlUi, and application-host targets" ON)

if(MOBAGEN_BUILD_RUNTIME)
  include(external/external.cmake)
endif()
```

```cmake
if(MOBAGEN_BUILD_RUNTIME)
  CPMAddPackage(
    NAME CPMLicenses.cmake
    GITHUB_REPOSITORY cpm-cmake/CPMLicenses.cmake
    VERSION 0.0.5
  )
  write_license_disclaimer("third_party.txt" "${CPM_PACKAGES}")
endif()
```

Keep GLM, doctest, and formatting support outside this guard because foundation headers and tests use them.

- [ ] **Step 3: Make the core umbrella conditional without changing its default**

In `core/CMakeLists.txt`, always add the foundation subdirectories. Add `sources/app`, `sources/imgui`, `sources/rmlui`, `_CORE_SDL_TARGET`, and the graphics/UI libraries only inside `if(MOBAGEN_BUILD_RUNTIME)`. Build the umbrella in two calls:

```cmake
target_link_libraries(
  core
  INTERFACE mobagen::core_ecs
            mobagen::core_jobs
            mobagen::core_reactive
            mobagen::core_messaging
            mobagen::core_scene
            mobagen::core_camera
            mobagen::core_input
            mobagen::core_render
            mobagen::core_resource
            mobagen::core_net
)

if(MOBAGEN_BUILD_RUNTIME)
  target_link_libraries(
    core
    INTERFACE mobagen::core_app
              mobagen::core_imgui
              ${_CORE_SDL_TARGET}
              SDL3_image::SDL3_image-static
              IMGUI
              dawn::webgpu
              ${RMLUI_LIBS}
  )
endif()
```

- [ ] **Step 4: Split the test executables by dependency boundary**

Replace the source glob in `test/CMakeLists.txt` with explicit source sets:

```cmake
set(
  FOUNDATION_TEST_SOURCES
  Test.cpp
  ecs_tests.cpp
  jobs_tests.cpp
  messaging_tests.cpp
  reactive_tests.cpp
  scene_tests.cpp
  subsystems_tests.cpp
)

add_executable(FoundationTests ${FOUNDATION_TEST_SOURCES})
target_compile_features(FoundationTests PRIVATE cxx_std_20)
target_link_libraries(
  FoundationTests
  PRIVATE doctest::doctest
          mobagen::core_ecs
          mobagen::core_jobs
          mobagen::core_reactive
          mobagen::core_messaging
          mobagen::core_scene
          mobagen::core_camera
          mobagen::core_input
          mobagen::core_render
          mobagen::core_resource
)
doctest_discover_tests(FoundationTests TEST_PREFIX "foundation::" PROPERTIES LABELS foundation)

add_custom_target(CoreTests DEPENDS FoundationTests)

if(TARGET mobagen::core_app)
  add_executable(AppHostTests Test.cpp app_host_tests.cpp)
  target_compile_features(AppHostTests PRIVATE cxx_std_23)
  target_link_libraries(AppHostTests PRIVATE doctest::doctest mobagen::core_app)
  doctest_discover_tests(AppHostTests TEST_PREFIX "app-host::" PROPERTIES LABELS app-host)
  add_dependencies(CoreTests AppHostTests)
endif()
```

- [ ] **Step 5: Fix the already reproduced MSVC header failure**

Add the standard header that owns `std::max` before `<cmath>`:

```cpp
#include <algorithm>
#include <cmath>
```

- [ ] **Step 6: Configure, build, and run the isolated suite**

Run:

```bash
cmake -S . -B build-foundation -DMOBAGEN_BUILD_RUNTIME=OFF -DBUILD_EXAMPLES=OFF -DENABLE_DOCUMENTATION=OFF -DENABLE_TEST_COVERAGE=ON
cmake --build build-foundation --config Debug --target FoundationTests --parallel 2
ctest --test-dir build-foundation -C Debug -L foundation --output-on-failure
```

Expected: configure does not fetch or configure SDL3/Dawn/ImGui/RmlUi, `FoundationTests` builds, and every discovered foundation test passes.

Run: `cmake --build build-baseline --config Debug --target CoreTests --parallel 2`

Expected: the default full-runtime composition remains buildable; `CoreTests` builds both `FoundationTests` and `AppHostTests`.

- [ ] **Step 7: Inspect and synchronize the focused change**

Run: `git diff --check && git status --short`

Expected: only the four listed files changed and no generated `third_party.txt` content is present.

```bash
git add CMakeLists.txt core/CMakeLists.txt core/sources/camera/camera.hpp test/CMakeLists.txt
git commit -m "build: isolate foundation test composition"
git push origin feature/modular-runtime
```

### Task 3: Make coverage instrumentation compiler-aware

**Files:**

- Create: `cmake/coverage.cmake`
- Modify: `test/CMakeLists.txt`

**Interfaces:**

- Consumes: CMake target names and `CMAKE_CXX_COMPILER_ID`.
- Produces: `mobagen_enable_coverage(<target>...)`, which instruments non-interface targets only under GNU or Clang and reports an explicit skip for MSVC.

- [ ] **Step 1: Preserve the current failing evidence**

Run: `cmake --build build-foundation --config Debug --target FoundationTests --verbose`

Expected before the change on MSVC: warnings show unsupported `-O0`, `-g`, `-fprofile-arcs`, and `-ftest-coverage` flags on `FoundationTests`.

- [ ] **Step 2: Add the coverage helper**

Create `cmake/coverage.cmake`:

```cmake
include_guard(GLOBAL)

function(mobagen_enable_coverage)
  if(NOT ENABLE_TEST_COVERAGE)
    return()
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target IN LISTS ARGN)
      if(NOT TARGET ${target})
        message(FATAL_ERROR "Cannot enable coverage for missing target '${target}'")
      endif()
      target_compile_options(${target} PRIVATE -O0 -g --coverage)
      target_link_options(${target} PRIVATE --coverage)
    endforeach()
  else()
    message(STATUS "Coverage instrumentation is unavailable for ${CMAKE_CXX_COMPILER_ID}; tests remain enabled")
  endif()
endfunction()
```

- [ ] **Step 3: Apply coverage to code that is actually compiled**

In `test/CMakeLists.txt`, remove the unconditional flag block, include the helper, and instrument the executable plus compiled libraries:

```cmake
include(../cmake/coverage.cmake)

mobagen_enable_coverage(FoundationTests mobagen_core_jobs)
if(TARGET AppHostTests)
  mobagen_enable_coverage(AppHostTests mobagen_core_app)
endif()
```

- [ ] **Step 4: Verify supported and unsupported compiler behavior**

Run on the current Windows host:

```bash
cmake -S . -B build-foundation -DMOBAGEN_BUILD_RUNTIME=OFF -DBUILD_EXAMPLES=OFF -DENABLE_TEST_COVERAGE=ON
cmake --build build-foundation --config Debug --target FoundationTests --verbose
```

Expected: PASS with an explicit MSVC skip message and no coverage flags on Mobagen targets.

Run in Linux CI with GCC: `cmake --build build --target FoundationTests --verbose`

Expected: Mobagen foundation objects and the test executable contain `--coverage`; third-party targets do not inherit it.

- [ ] **Step 5: Commit and synchronize**

```bash
git add cmake/coverage.cmake test/CMakeLists.txt
git commit -m "build: scope coverage flags by compiler"
git push origin feature/modular-runtime
```

### Task 4: Separate ECS correctness from repeatable performance measurement

**Files:**

- Modify: `CMakeLists.txt`
- Modify: `test/ecs_tests.cpp`
- Create: `benchmarks/CMakeLists.txt`
- Create: `benchmarks/benchmark_runner.hpp`
- Create: `benchmarks/foundation_baseline.cpp`

**Interfaces:**

- Consumes: `ecs::World::view`, `ecs::World::apply_range`, `jobs::Scheduler::parallel_for`, `render::RenderBridge::build`.
- Produces: deterministic `apply_range` coverage and `MobagenFoundationBenchmark --warmup <N> --samples <N>` emitting JSON schema `mobagen.foundation-benchmark.v1` to stdout.

- [ ] **Step 1: Replace the misleading timing assertion with a failing correctness case**

Remove `<chrono>`, the unused scheduler construction, and `TEST_CASE("Perf: 2M entity parallel_for >= 3x faster than serial")`. Add:

```cpp
TEST_CASE("World: apply_range updates only the requested dense interval") {
  ecs::World world;
  for (int i = 0; i < 8; ++i) {
    const auto entity = world.create();
    world.add<Position>(entity, static_cast<float>(i), 0.0f, 0.0f);
    world.add<Velocity>(entity, 10.0f, 0.0f);
  }

  world.apply_range<Position, Velocity>(2, 5, [](auto, Position& position, Velocity& velocity) {
    position.x += velocity.vx;
  });

  int dense_index = 0;
  world.view<Position>([&](auto, Position& position) {
    const float expected = dense_index >= 2 && dense_index < 5
                               ? static_cast<float>(dense_index) + 10.0f
                               : static_cast<float>(dense_index);
    CHECK(position.x == expected);
    ++dense_index;
  });
}
```

- [ ] **Step 2: Run the deterministic foundation tests**

Run: `cmake --build build-foundation --config Debug --target FoundationTests --parallel 2 && ctest --test-dir build-foundation -C Debug -L foundation --output-on-failure`

Expected: PASS without a timing-based `WARN` and without allocating 200,000 entities during ordinary unit tests.

- [ ] **Step 3: Add an opt-in benchmark target**

Add to the root file:

```cmake
option(MOBAGEN_BUILD_BENCHMARKS "Build Mobagen performance baseline executables" OFF)
if(MOBAGEN_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
```

Create `benchmarks/CMakeLists.txt`:

```cmake
add_executable(MobagenFoundationBenchmark foundation_baseline.cpp)
target_compile_features(MobagenFoundationBenchmark PRIVATE cxx_std_20)
target_link_libraries(
  MobagenFoundationBenchmark
  PRIVATE mobagen::core_ecs
          mobagen::core_jobs
          mobagen::core_scene
          mobagen::core_render
)
```

- [ ] **Step 4: Implement reusable sampling and statistics**

Create `benchmark_runner.hpp` with these public types and functions:

```cpp
namespace mobagen::benchmark {
  struct Options {
    std::size_t warmup = 5;
    std::size_t samples = 30;
  };

  struct Result {
    std::string name;
    std::vector<double> samples_ns;
    double median_ns = 0.0;
    double p95_ns = 0.0;
  };

  Options parse_options(int argc, char** argv);
  double percentile(std::vector<double> values, double quantile);

  template <class Fn>
  Result measure(std::string name, const Options& options, Fn&& operation) {
    for (std::size_t i = 0; i < options.warmup; ++i) operation();
    Result result{std::move(name)};
    result.samples_ns.reserve(options.samples);
    for (std::size_t i = 0; i < options.samples; ++i) {
      const auto begin = std::chrono::steady_clock::now();
      operation();
      const auto end = std::chrono::steady_clock::now();
      result.samples_ns.push_back(
          std::chrono::duration<double, std::nano>(end - begin).count());
    }
    result.median_ns = percentile(result.samples_ns, 0.50);
    result.p95_ns = percentile(result.samples_ns, 0.95);
    return result;
  }

  void write_json(std::ostream& output, const Options& options, std::span<const Result> results);
}
```

`parse_options` must reject zero values, unknown flags, missing values, and non-numeric values with exit code 2. `percentile` sorts a copy and uses the nearest-rank index `ceil(q * size) - 1`. `write_json` emits finite numeric values, raw samples, median, and p95 under schema `mobagen.foundation-benchmark.v1`.

- [ ] **Step 5: Implement the five foundation workloads**

In `foundation_baseline.cpp`, preallocate and warm all working data before measurement, then emit these exact result names:

```cpp
const auto results = std::array{
    measure("startup.foundation", options, construct_foundation_state),
    measure("ecs.serial_update", options, update_positions),
    measure("jobs.parallel_for", options, run_parallel_chunks),
    measure("render.bridge_build", options, build_render_commands),
    measure("frame.foundation", options, run_representative_frame),
};
write_json(std::cout, options, results);
```

Use 200,000 entities for ECS and 10,000 renderable entities. `run_parallel_chunks` must use `Scheduler::parallel_for`, disjoint vector ranges, a grain size of 1,024, `WaitGroup`, and `Scheduler::wait`. The representative frame performs one ECS position update followed by one `RenderBridge::build`. Reserve destination storage through existing public APIs where available; record any unavoidable post-warm-up allocation in the baseline report rather than hiding it.

- [ ] **Step 6: Build and validate benchmark behavior**

Run:

```bash
cmake -S . -B build-foundation -DMOBAGEN_BUILD_RUNTIME=OFF -DBUILD_EXAMPLES=OFF -DMOBAGEN_BUILD_BENCHMARKS=ON -DENABLE_TEST_COVERAGE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-foundation --config Release --target MobagenFoundationBenchmark --parallel 2
build-foundation/bin/Release/MobagenFoundationBenchmark.exe --warmup 2 --samples 5
```

Expected: exit 0 and valid JSON containing exactly five results, five raw samples per result, and positive median/p95 values.

Run: `build-foundation/bin/Release/MobagenFoundationBenchmark.exe --samples 0`

Expected: exit 2 with a concise invalid-value diagnostic and no partial JSON.

- [ ] **Step 7: Commit and synchronize**

```bash
git add CMakeLists.txt test/ecs_tests.cpp benchmarks/CMakeLists.txt benchmarks/benchmark_runner.hpp benchmarks/foundation_baseline.cpp
git commit -m "perf: add foundation baseline harness"
git push origin feature/modular-runtime
```

### Task 5: Capture reproducible baseline evidence

**Files:**

- Create: `scripts/baseline.py`
- Create: `test/python/test_baseline_script.py`
- Create: `docs/performance/baselines/2026-09-13-windows-msvc-release.json`
- Create: `docs/performance/2026-09-13-foundation-baseline.md`

**Interfaces:**

- Consumes: benchmark JSON schema `mobagen.foundation-benchmark.v1` on stdout.
- Produces: evidence schema `mobagen.baseline.v1` with Git commit, dirty state, UTC timestamp, OS, architecture, processor, Python version, CMake version, compiler/build labels, benchmark command, and nested benchmark output.

- [ ] **Step 1: Write capture-script contract tests**

Use `unittest`, a temporary executable Python fixture, and `subprocess` to cover:

```python
class BaselineScriptTest(unittest.TestCase):
    def test_rejects_non_json_benchmark_output(self) -> None:
        result = run_capture(fixture="print('not json')")
        self.assertEqual(result.returncode, 1)
        self.assertIn("benchmark did not emit valid JSON", result.stderr)

    def test_wraps_valid_benchmark_with_provenance(self) -> None:
        result, output = run_valid_capture()
        self.assertEqual(result.returncode, 0)
        self.assertEqual(output["schema"], "mobagen.baseline.v1")
        self.assertEqual(output["benchmark"]["schema"], "mobagen.foundation-benchmark.v1")
        self.assertIn("git", output)
        self.assertIn("platform", output)
        self.assertIn("toolchain", output)
```

The fixture helper writes only inside `tempfile.TemporaryDirectory` and invokes the production script with `sys.executable`.

- [ ] **Step 2: Run the tests and confirm the script is absent**

Run: `python -m unittest discover -s test/python -p "test_baseline_script.py" -v`

Expected: FAIL because `scripts/baseline.py` does not exist.

- [ ] **Step 3: Implement strict capture and atomic output**

The CLI is:

```text
python scripts/baseline.py
  --binary <path>
  --output <path>
  --configuration <label>
  --compiler <label>
  --warmup <positive-int>
  --samples <positive-int>
```

Run the binary without a shell, validate its exit status and schema, obtain Git data with argument-list subprocesses, write UTF-8 JSON with sorted keys and two-space indentation to a temporary sibling, then use `Path.replace()` for atomic publication. A failed benchmark or invalid JSON must leave an existing output file byte-for-byte unchanged.

- [ ] **Step 4: Verify script failure and success paths**

Run: `python -m unittest discover -s test/python -p "test_baseline_script.py" -v`

Expected: all capture tests PASS.

- [ ] **Step 5: Capture the current Windows baseline**

Run:

```bash
python scripts/baseline.py --binary build-foundation/bin/Release/MobagenFoundationBenchmark.exe --output docs/performance/baselines/2026-09-13-windows-msvc-release.json --configuration Release --compiler "MSVC 19.51" --warmup 5 --samples 30
```

Expected: valid tracked JSON with 30 raw samples for each of the five measurements and `dirty: false` at capture time. If capture follows uncommitted script work, first commit/push code, capture at that clean commit, then commit the evidence separately as required below.

- [ ] **Step 6: Write the human-readable baseline report**

Record:

- base commit `4dd5d889b666463ae6c36c4b4245a9c8eb6e1208` (`v1.23.2`);
- host/toolchain fields copied from the generated evidence;
- original configure time and the fact that `CoreTests` pulled Dawn/Tint/SDL3/ImGui/RmlUi;
- the original MSVC failure at `core/sources/camera/camera.hpp:248` for missing `std::max` ownership;
- the ignored GCC coverage flags observed before Task 3;
- the exact foundation configure/build/test/capture commands;
- a table of median and p95 values copied from the generated evidence;
- limitations: one Windows host, Debug build failure evidence vs Release performance evidence, and no GPU submission timing in the foundation-only harness.

- [ ] **Step 7: Commit code, then evidence, synchronizing each**

```bash
git add scripts/baseline.py test/python/test_baseline_script.py
git commit -m "test: add reproducible baseline capture"
git push origin feature/modular-runtime

git add docs/performance/baselines/2026-09-13-windows-msvc-release.json docs/performance/2026-09-13-foundation-baseline.md
git commit -m "docs: record foundation performance baseline"
git push origin feature/modular-runtime
```

### Task 6: Enforce the baseline in CI

**Files:**

- Modify: `.github/workflows/test.yml`

**Interfaces:**

- Consumes: `MOBAGEN_BUILD_RUNTIME`, `FoundationTests`, `AppHostTests`, CTest labels `foundation` and `app-host`, and Python policy tests.
- Produces: a three-OS fast foundation gate and one Linux full-runtime integration gate.

- [ ] **Step 1: Demonstrate current CI omissions**

Inspect `.github/workflows/test.yml` and record that Windows is commented out, `CoreTests` builds the graphics/UI graph, and no asset policy test runs.

Expected: all three omissions are present before the edit.

- [ ] **Step 2: Define the foundation matrix**

Use an explicit include matrix so only GCC collects coverage:

```yaml
strategy:
  fail-fast: false
  matrix:
    include:
      - os: ubuntu-latest
        coverage: ON
      - os: macos-latest
        coverage: OFF
      - os: windows-2022
        coverage: OFF
```

Configure and execute:

```yaml
- name: Configure foundation
  run: >-
    cmake -S . -B build-foundation
    -DMOBAGEN_BUILD_RUNTIME=OFF
    -DBUILD_EXAMPLES=OFF
    -DENABLE_DOCUMENTATION=OFF
    -DENABLE_TEST_COVERAGE=${{ matrix.coverage }}
    -DCMAKE_BUILD_TYPE=Debug

- name: Build foundation
  run: cmake --build build-foundation --config Debug --parallel 2 --target FoundationTests

- name: Test foundation
  run: ctest --test-dir build-foundation -C Debug -L foundation --output-on-failure

- name: Test asset policy
  run: python -m unittest discover -s test/python -p "test_*policy.py" -v
```

- [ ] **Step 3: Preserve a bounded app-host integration gate**

Add a separate `runtime-integration` job on `ubuntu-latest` that configures with `MOBAGEN_BUILD_RUNTIME=ON`, builds only `AppHostTests` with parallelism 2, and runs `ctest -L app-host`. Retain the existing Linux dependency installation and CPM cache for this job.

- [ ] **Step 4: Validate workflow syntax and local commands**

Run: `git diff --check`

Run the exact Windows foundation configure/build/CTest and Python policy commands locally.

Expected: all local commands PASS, and the diff contains no release/deployment changes.

After push, inspect the GitHub Actions checks for the branch.

Expected: foundation jobs pass on Windows, Linux, and macOS; the Linux app-host job builds and passes independently.

- [ ] **Step 5: Commit and synchronize**

```bash
git add .github/workflows/test.yml
git commit -m "ci: add cross-platform foundation baseline"
git push origin feature/modular-runtime
```

## Milestone 0 Completion Gate

Milestone 0 is complete only when:

1. The original checkout's untracked asset inventory is unchanged.
2. The feature worktree is clean and tracks `origin/feature/modular-runtime`.
3. The isolated foundation configure does not fetch or build SDL3, Dawn, ImGui, or RmlUi.
4. Foundation tests pass locally on Windows and in CI on Windows, Linux, and macOS.
5. App-host integration remains green in its full-runtime Linux job.
6. Unsupported coverage flags are absent on MSVC, and GCC instruments Mobagen code rather than third parties.
7. The unit suite has no timing-based performance assertion.
8. The tracked baseline contains raw samples, median, p95, Git provenance, platform provenance, and toolchain provenance.
9. Every completed task has its own focused commit on the remote feature branch.
