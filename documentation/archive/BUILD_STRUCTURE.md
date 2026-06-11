# Build Directory Structure

## New Organization

All builds now go into `build/` with clear subdirectories:

```
build/
├── native/
│   ├── bin/               → Executable + resources
│   └── libs/              → Static libraries
├── wasm-webgl/
│   ├── bin/               → dicom_renderer.html, .js, .wasm
│   └── libs/
└── wasm-webgpu/
    ├── bin/               → dicom_renderer.html, .js, .wasm
    └── libs/
```

## Quick Build Commands

### Native (Desktop)
```bash
cd e:\repositories\game-guild\mobagen
rm -r build/native -Force
cmake -B build/native -DCMAKE_BUILD_TYPE=Release -G "Visual Studio 16 2019"
cmake --build build/native --config Release
```

### WASM WebGL (G2)
```bash
cd e:\repositories\game-guild\mobagen
rm -r build/wasm-webgl -Force
emcmake cmake -B build/wasm-webgl -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm-webgl
cd build/wasm-webgl/bin && python -m http.server 8083
```

### WASM WebGPU (G3)
```bash
cd e:\repositories\game-guild\mobagen
rm -r build/wasm-webgpu -Force
emcmake cmake -B build/wasm-webgpu -DUSE_WEBGPU=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm-webgpu
cd build/wasm-webgpu/bin && python -m http.server 8084
```

## Clean Up Old Builds

```powershell
cd e:\repositories\game-guild\mobagen

# Remove all old build directories
Get-ChildItem -Directory -Filter "build-*" | Remove-Item -Recurse -Force
Get-ChildItem -Name -Filter "build-*.log" | Remove-Item

echo "Old builds cleaned. New structure ready."
```

## Output Locations

| Build Type | Output Location | Run Command |
|-----------|-----------------|------------|
| Native | `build/native/bin/dicom_renderer.exe` | `./build/native/bin/dicom_renderer.exe` |
| WASM WebGL | `build/wasm-webgl/bin/dicom_renderer.html` | `cd build/wasm-webgl/bin && python -m http.server 8083` |
| WASM WebGPU | `build/wasm-webgpu/bin/dicom_renderer.html` | `cd build/wasm-webgpu/bin && python -m http.server 8084` |

## CMakeLists.txt Changes

The `CMakeLists.txt` now automatically detects the build type and organizes output:

```cmake
if(EMSCRIPTEN)
    if(USE_WEBGPU)
        set(BUILD_TYPE_DIR "wasm-webgpu")
    else()
        set(BUILD_TYPE_DIR "wasm-webgl")
    endif()
else()
    set(BUILD_TYPE_DIR "native")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${BUILD_TYPE_DIR}/bin)
```

This means:
- `cmake -B build/native ...` → outputs to `build/native/bin`
- `cmake -B build/wasm-webgl ...` → outputs to `build/wasm-webgl/bin`
- `cmake -B build/wasm-webgpu ...` → outputs to `build/wasm-webgpu/bin`

## Benefits

✅ **Clear organization** — Easy to find builds by type
✅ **Parallel builds** — All versions can coexist without conflict
✅ **No confusion** — No more `build-wasm-unified` vs `build-wasm-fresh` ambiguity
✅ **Scalable** — Easy to add new build variants (debug, profiling, etc.)
✅ **Cross-platform** — Works on Windows, Linux, macOS
