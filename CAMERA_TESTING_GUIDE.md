# Camera System Testing Guide

## Current Status

✅ **Camera system integrated into C++ code:**
- Global camera instance with Orbit + WASD modes
- SDL2 input handlers (keyboard, mouse, wheel)
- Shader integration (view_projection matrix)
- Viewport resize handling

⏳ **WASM build**: Environmental setup needed (Emscripten/Ninja config)

✅ **Test version**: Pure JavaScript demo available NOW

---

## Test Option 1: JavaScript Demo (Works Now)

### Open in Browser
```
http://localhost:8084/test.html
```

### Test Orbit Mode
```
Click + Drag         → Rotate around square
Mouse Wheel Up/Down  → Zoom in/out
Press C              → Switch to WASD mode
```

**Expected:** Square rotates when you drag, zooms with wheel

### Test WASD Mode
```
W/A/S/D              → Move forward/left/back/right
Mouse Drag           → Look around
Space                → Move up
Mouse Wheel          → Speed changes
Press C              → Switch back to Orbit
```

**Expected:** You move through 3D space, square stays in view

### What This Proves
- ✅ Camera math works (matrix generation)
- ✅ Input handling works (keyboard + mouse)
- ✅ 3D projection works (perspective view)
- ✅ Mode switching works (Orbit ↔ WASD)

---

## Test Option 2: WASM Build (When Environment is Ready)

### Prerequisites
```powershell
# Emscripten must be installed and activated
$env:EMSDK = "C:\Users\MatheusMartins\AppData\Local\Temp\emsdk"
$env:PATH = "$env:EMSDK;$env:EMSDK\upstream\emscripten;$env:EMSDK\node\22.16.0_64bit\bin;$env:PATH"

# Verify
emcc --version
```

### Build Steps
```bash
cd e:\repositories\game-guild\mobagen

# Clean old build
rm -r build-wasm-unified -Force

# Configure (one of these):

# Option A: Visual Studio (Windows native)
cmake -B build-wasm-unified -DCMAKE_BUILD_TYPE=Release -G "Visual Studio 16 2019"
cmake --build build-wasm-unified

# Option B: Emscripten + Ninja
emcmake cmake -B build-wasm-unified -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm-unified

# Run
cd build-wasm-unified/bin
python -m http.server 8083
```

### Open WASM App
```
http://localhost:8083/dicom_renderer.html
```

### Test in Browser
Same tests as above (Orbit mode, WASD mode, etc.)

**Additional:** Will show real-time FPS + renderer info (G2/G3)

---

## Camera Implementation Details

### Architecture

```cpp
// Global camera instance (main.cpp)
static engine::Camera g_camera(engine::CameraMode::ORBIT);

// Input handling (both AppWebGL and AppWebGPU)
SDL_KEYDOWN    → camera.on_key_pressed(key)
SDL_MOUSEMOTION → camera.on_mouse_motion(dx, dy)
SDL_MOUSEWHEEL  → camera.on_mouse_wheel(y)

// Rendering
shader->use();
shader->setUniform("view_projection", g_camera.get_view_projection());
```

### Camera Modes

#### Orbit Mode
```
Position: Circles around focal_point (0,0,0)
Distance: Controlled by mouse wheel
Rotation: Yaw/pitch from mouse drag
Formula:  x = radius * sin(yaw) * cos(pitch)
          y = radius * sin(pitch)
          z = radius * cos(yaw) * cos(pitch)
```

#### WASD Mode
```
Position: Moves freely with WASD + Space
Direction: Controlled by mouse drag (yaw/pitch)
Speed: Adjustable with mouse wheel
Formula:  position += (forward * speed * dt) when W pressed
          position += (right * speed * dt) when D pressed
```

### Input Mapping

| Input | Action | Orbit | WASD |
|-------|--------|-------|------|
| Mouse Drag | Camera control | Rotate | Look |
| Wheel Up | Zoom/Speed | Zoom in | Speed up |
| Wheel Down | Zoom/Speed | Zoom out | Speed down |
| W/A/S/D | Move | - | Move |
| Space | Up | - | Move up |
| C | Mode toggle | → WASD | → Orbit |

---

## Shader Integration

### Vertex Shader
```glsl
#version 300 es
uniform mat4 view_projection;
layout(location = 0) in vec2 aPos;

void main() {
    gl_Position = view_projection * vec4(aPos, 0.0, 1.0);
}
```

### Matrix Flow
```
3D Position (model space)
    ↓
Multiply by view_projection matrix
    ↓
Clip space (ready for rasterization)
```

---

## Testing Checklist

### JavaScript Demo (http://localhost:8084/test.html)
- [ ] Orbit mode: Click+drag rotates square
- [ ] Orbit mode: Wheel zooms in/out
- [ ] Orbit mode: Zoom min/max works (0.1 to 100 units)
- [ ] WASD mode: Press C to switch
- [ ] WASD mode: WASD keys move through space
- [ ] WASD mode: Mouse drag changes view direction
- [ ] WASD mode: Press C to return to Orbit
- [ ] Speed changes: Wheel adjusts speed in WASD mode
- [ ] Both modes work smoothly without stuttering

### WASM Build (Once environment is ready)
- [ ] Same tests as above
- [ ] FPS displays correctly (should be ~60)
- [ ] Renderer info shows "WebGL (G2)" or "WebGPU (G3)"
- [ ] Works in both G2 and G3 renderers
- [ ] Canvas resize: Drag browser window corners, camera aspect updates

---

## Troubleshooting

### JavaScript Demo Doesn't Work
- Check browser console (F12) for errors
- Ensure http://localhost:8084 is accessible
- Try hard refresh (Ctrl+Shift+R)

### WASM Build Fails
**Issue**: CMake can't find Ninja
```powershell
# Solution: Use Visual Studio generator
cmake -B build-wasm-unified -DCMAKE_BUILD_TYPE=Release -G "Visual Studio 16 2019"
```

**Issue**: emcmake not found
```powershell
# Activate Emscripten environment first
$env:EMSDK = "C:\Users\MatheusMartins\AppData\Local\Temp\emsdk"
$env:PATH = "$env:EMSDK;...other paths...;$env:PATH"
```

**Issue**: "The process cannot access the file because it is being used by another process"
```powershell
# Restart PowerShell or command prompt
# Or use Task Manager to kill cmake/ninja processes
```

### Camera Doesn't Respond
- Ensure window has focus (click on canvas)
- Check that you're not holding any modifier keys
- Try pressing 'C' to toggle mode (might help reset state)

### Canvas Doesn't Resize
- JavaScript demo is fixed 800x600
- WASM version should resize with window
- Hard refresh browser if stuck

---

## Next Steps

### Immediate (This Week)
1. ✅ Integrate camera (DONE)
2. ⏳ Test camera in WASM (waiting on build env)
3. 📝 Document camera behavior (DONE - you're reading it!)

### Short-term (Week 6)
- Refactor triangle → 3D cube (uses camera perspective)
- Add lighting based on surface normal
- Test camera with multi-object scene

### Long-term (Week 7-8)
- Replace geometry with implicit sphere (ray marching test)
- Add 3D texture sampling
- Implement volume rendering shader
- Load DICOM data

---

## Code References

- **Camera class**: `src/engine/camera.h`
- **Integration**: `src/main.cpp` (lines 44-52, 330-390, 460-495)
- **Shader**: `html/shell.html` (WebGL) + WebGPU WGSL
- **Test**: `test.html` (JavaScript demo)

---

## Performance Notes

### Frame Rate
- JavaScript demo: Should hold 60 FPS (GPU-limited on simple geometry)
- WASM WebGL: Should hold 60 FPS (similar to JS)
- WASM WebGPU: Should hold 60 FPS (deferred batching helps with complex scenes)

### Camera Updates
- Every frame (60 FPS = ~16.7ms per frame)
- Camera position/matrices recalculated each frame
- Input is polled via SDL2 (native) or DOM (WASM via Emscripten)

### No Noticeable Lag
If you experience stuttering:
1. Check browser console for WebGL errors (F12)
2. Verify hardware acceleration is enabled
3. Close other demanding applications
4. Try different browser (Chrome, Firefox, Edge)

---

## Questions?

For implementation details, see:
- `WEEK4-8_ROADMAP.md` - Architecture and next phases
- `src/engine/camera.h` - Camera class documentation
- `src/main.cpp` - Integration points

All code is heavily commented for learning!
