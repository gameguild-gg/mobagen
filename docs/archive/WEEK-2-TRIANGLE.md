# Week 2 — Triangle in the Browser

## The Challenge

Render an actual **3D triangle** on the GPU, not just a cleared canvas.

```
Week 1: ✅ Colored canvas (blue background)
Week 2: ➕ Triangle (teal color, 3D geometry, shaders)
```

This proves the **complete graphics pipeline** works:
- Geometry → GPU
- Shaders compile & link
- Draw call executes
- Result appears on screen

---

## What We're Building

### Visual Result

```
        (0, 0.5)
            △
           / \
          /   \
         /     \
        /       \
  (-0.5,-0.5)   (0.5,-0.5)
```

- **3 vertices** in NDC (Normalized Device Coordinates)
- **Teal color** (RGB: 0, 1, 0.5)
- **Fills ~25% of screen**
- **No rotation or scaling**

### The Graphics Pipeline

```
C++ Code
  ↓
Vertex Data (3 vertices × 2 floats each)
  ↓ glGenBuffers + glBufferData
GPU Memory (Vertex Buffer Object)
  ↓ glGenVertexArrays + glVertexAttribPointer
Vertex Array Object (describes data layout)
  ↓
Shader Program (vertex + fragment shaders)
  ↓ glLinkProgram
Compiled executable on GPU
  ↓
glDrawArrays(GL_TRIANGLES, 0, 3)
  ↓ GPU executes:
  │  1. Vertex shader runs once per vertex (3 times)
  │  2. Rasterization fills triangle with pixels
  │  3. Fragment shader runs once per pixel
  ↓
Frame Buffer (displayed on screen)
```

---

## The Code: Step by Step

### Part 1: Shader Source Code

#### Shader Version Branching

We need **different shader versions** for WASM vs native:

```cpp
#ifdef __EMSCRIPTEN__
  static constexpr const char* VERT_GLSL = "#version 300 es\n";
  static constexpr const char* FRAG_GLSL = "#version 300 es\nprecision mediump float;\n";
#else
  static constexpr const char* VERT_GLSL = "#version 330 core\n";
  static constexpr const char* FRAG_GLSL = "#version 330 core\n";
#endif
```

**Why the difference?**

| Platform | Version | Meaning |
|----------|---------|---------|
| **WASM** | `#version 300 es` | GLES3 (OpenGL ES, for embedded systems/mobile) |
| **Native** | `#version 330 core` | Desktop OpenGL 3.30 (core profile = no deprecated features) |
| **WASM Fragment** | `precision mediump float` | Mobile GPUs are weaker; hint to use medium precision |

#### Vertex Shader

```glsl
#version 300 es
layout(location = 0) in vec2 aPos;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
```

**Line by line:**

```glsl
layout(location = 0) in vec2 aPos;
```
- **`layout(location = 0)`** — This is the first vertex attribute (index 0)
- **`in vec2 aPos`** — Input: 2D position from CPU (x, y)
- We'll bind vertex data to this input

```glsl
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
```
- **Input:** `aPos` is a `vec2` (e.g., `(0.5, -0.5)`)
- **Transform:** Create a `vec4` by adding z=0.0 and w=1.0
- **Output:** `gl_Position` is the **built-in** output to the GPU
- **Result:** Position in **homogeneous coordinates** (required by GPU)

#### Fragment Shader

```glsl
#version 300 es
precision mediump float;
out vec4 fragColor;

void main() {
    fragColor = vec4(0.0, 1.0, 0.5, 1.0);
}
```

**Line by line:**

```glsl
precision mediump float;
```
- **GLES3 requirement** (mobile GPU optimization hint)
- Not needed on desktop OpenGL, included anyway

```glsl
out vec4 fragColor;
```
- **Output:** Final pixel color
- **`vec4`:** (red, green, blue, alpha)
- GPU writes this to the frame buffer

```glsl
fragColor = vec4(0.0, 1.0, 0.5, 1.0);
```
- **Red:** 0.0 (no red)
- **Green:** 1.0 (full green)
- **Blue:** 0.5 (medium blue)
- **Alpha:** 1.0 (fully opaque)
- **Result:** Teal color

**Try modifying:**
```glsl
fragColor = vec4(1.0, 0.0, 0.0, 1.0);  // Red triangle
fragColor = vec4(1.0, 1.0, 0.0, 1.0);  // Yellow triangle
fragColor = vec4(0.5, 0.5, 0.5, 1.0);  // Gray triangle
```

---

### Part 2: Shader Compilation

#### Compile a Single Shader

```cpp
GLuint vert = glCreateShader(GL_VERTEX_SHADER);
const char* vertPtr = vertSrc.c_str();
glShaderSource(vert, 1, &vertPtr, nullptr);
glCompileShader(vert);
```

**Step by step:**

```cpp
GLuint vert = glCreateShader(GL_VERTEX_SHADER);
```
- Create an empty shader object
- `GLuint` is a GPU-side handle (unsigned integer ID)

```cpp
glShaderSource(vert, 1, &vertPtr, nullptr);
```
- Feed the source code to the shader
- `1` = one string
- `&vertPtr` = pointer to the string
- `nullptr` = string is null-terminated (auto-length)

```cpp
glCompileShader(vert);
```
- Compile the source code to GPU bytecode
- This happens **on the GPU**, very fast

#### Check for Errors

```cpp
int success;
glGetShaderiv(vert, GL_COMPILE_STATUS, &success);
if (!success) {
    char infoLog[512];
    glGetShaderInfoLog(vert, 512, nullptr, infoLog);
    fprintf(stderr, "Vertex shader compilation failed: %s\n", infoLog);
    glDeleteShader(vert);
    return false;
}
```

**What's happening:**

```cpp
glGetShaderiv(vert, GL_COMPILE_STATUS, &success);
```
- Query the shader status
- `iv` = "integer vector" (get integer value)
- `GL_COMPILE_STATUS` = did compilation succeed?
- Result goes into `success` variable

```cpp
glGetShaderInfoLog(vert, 512, nullptr, infoLog);
```
- If compilation failed, get the error message
- Error messages are **crucial for debugging**

**Example error:**
```
ERROR: 0:4: 'aPos' : undeclared variable
ERROR: 0:4: 'assign' :  cannot convert from 'const 4-component vector of float' to 'float'
```

This tells you exactly what went wrong and where (line 4).

#### Link the Program

```cpp
shader = glCreateProgram();
glAttachShader(shader, vert);
glAttachShader(shader, frag);
glLinkProgram(shader);
```

**What's happening:**

```cpp
shader = glCreateProgram();
```
- Create an empty program object
- A program is a **linked executable** on the GPU

```cpp
glAttachShader(shader, vert);
glAttachShader(shader, frag);
```
- Attach compiled vertex + fragment shaders

```cpp
glLinkProgram(shader);
```
- Link them together (GPU executable format)
- Similar to C/C++ compiler's link stage

#### Delete Shader Objects

```cpp
glDeleteShader(vert);
glDeleteShader(frag);
```

After linking, the shader objects are no longer needed. Delete them to free GPU memory.

---

### Part 3: Vertex Buffer Setup

#### Define Geometry

```cpp
float vertices[] = {
    0.0f,  0.5f,    // Top vertex (x, y)
   -0.5f, -0.5f,    // Bottom-left
    0.5f, -0.5f     // Bottom-right
};
```

**Coordinates:**
- Each vertex is 2 floats (x, y)
- Total: 6 floats, 3 vertices
- Values are in **NDC** (Normalized Device Coordinates): -1 to +1

**Visualization:**
```
NDC Space:
  (-1, 1)  ←─────────────→ (1, 1)
     ┌────────────────────┐
     │                    │
     │        (0, 0.5)    │  ← Top vertex
     │           /\       │
     │          /  \      │
     │         /    \     │
     │        /      \    │
     │       /        \   │
     │      /          \  │
     │ (-0.5,-0.5)  (0.5,-0.5)
     │                    │
  (-1, -1) ←─────────────→ (1, -1)
```

#### Create & Upload Buffer

```cpp
GLuint vbo;
glGenBuffers(1, &vbo);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
```

**Step by step:**

```cpp
glGenBuffers(1, &vbo);
```
- Create 1 GPU buffer object
- `vbo` = Vertex Buffer Object (a handle)

```cpp
glBindBuffer(GL_ARRAY_BUFFER, vbo);
```
- **Bind** the buffer (make it the "active" buffer)
- All subsequent operations target this buffer
- Think: "Select this buffer for editing"

```cpp
glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
```
- Upload data to GPU
- `sizeof(vertices)` = 24 bytes (6 floats × 4 bytes/float)
- `vertices` = pointer to CPU data
- `GL_STATIC_DRAW` = data won't change (optimization hint)

#### Describe Data Layout (VAO)

```cpp
GLuint vao;
glGenVertexArrays(1, &vao);
glBindVertexArray(vao);

glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
glEnableVertexAttribArray(0);
```

**What's happening:**

```cpp
glGenVertexArrays(1, &vao);
glBindVertexArray(vao);
```
- Create a Vertex Array Object (VAO)
- VAOs remember how to interpret vertex data
- Bind it (make it active)

```cpp
glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
```

Breaking it down:
- **`0`** — Attribute index (matches `layout(location = 0)` in vertex shader)
- **`2`** — Each vertex has 2 floats (x, y)
- **`GL_FLOAT`** — Data type is floating-point
- **`GL_FALSE`** — Don't normalize data (keep as-is)
- **`2 * sizeof(float)`** — **Stride** = bytes between consecutive vertices
  - Vertex 1 is at byte 0-7 (2 floats)
  - Vertex 2 is at byte 8-15 (next 2 floats)
  - Distance between them: 8 bytes
- **`(void*)0`** — Start reading from byte 0 in the buffer

```cpp
glEnableVertexAttribArray(0);
```
- Enable attribute 0 (the position input)
- Without this, the shader won't receive data

---

### Part 4: The Draw Call

```cpp
glUseProgram(shader);
glBindVertexArray(vao);
glDrawArrays(GL_TRIANGLES, 0, 3);
```

**Step by step:**

```cpp
glUseProgram(shader);
```
- Activate the shader program
- All subsequent draws use this program

```cpp
glBindVertexArray(vao);
```
- Bind the vertex array
- GPU now knows how to read the vertex data

```cpp
glDrawArrays(GL_TRIANGLES, 0, 3);
```
- **Draw** the geometry
- `GL_TRIANGLES` — Treat vertices as triangle corners
- `0` — Start from vertex 0
- `3` — Draw 3 vertices

**What the GPU does:**
1. Fetch vertex 0, 1, 2 from the buffer
2. Execute vertex shader 3 times:
   - Input: each vertex position
   - Output: `gl_Position` for each
3. Rasterize: Fill the triangle with pixels
4. For each pixel inside the triangle, execute fragment shader:
   - Output: `fragColor` for each pixel
5. Write colors to frame buffer
6. Display on screen

---

## Putting It All Together: The App Struct

### Initialization

```cpp
struct App {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint shader = 0;
    bool running = true;

    bool init() {
        // 1. Initialize SDL + create context (Week 1 code)
        SDL_Init(SDL_INIT_VIDEO);
        // ... SDL_CreateWindow, SDL_GL_CreateContext, etc.

        // 2. Compile shaders
        if (!compileShaders()) return false;

        // 3. Setup geometry
        if (!setupGeometry()) return false;

        return true;
    }

    void tick() {
        // Clear blue background
        glClearColor(0.1f, 0.2f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Draw the triangle
        glUseProgram(shader);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Show frame
        SDL_GL_SwapWindow(window);
    }

    void cleanup() {
        // Delete GPU resources
        glDeleteProgram(shader);
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
};
```

---

## Debugging: When the Triangle Doesn't Appear

### Triangle is Black/Invisible

**Cause:** Shader compilation or linking failed
**Debug:**
```cpp
// In compileShaders(), check error logs
glGetShaderInfoLog(vert, 512, nullptr, infoLog);
fprintf(stderr, "VERT ERROR: %s\n", infoLog);

glGetProgramInfoLog(shader, 512, nullptr, infoLog);
fprintf(stderr, "LINK ERROR: %s\n", infoLog);
```

### Triangle is White Instead of Teal

**Cause:** Fragment shader not executing or returning wrong color
**Check:**
- Did you change `fragColor`?
- Syntax error in shader?

### Nothing on Screen (Black Canvas)

**Cause:** VAO or VBO not set up correctly
**Checklist:**
- [ ] `glVertexAttribPointer` called with correct stride
- [ ] `glEnableVertexAttribArray(0)` called
- [ ] `glBindVertexArray(vao)` before `glDrawArrays()`

### Segmentation Fault (Crash)

**Cause:** Using shader before it's compiled
**Fix:**
```cpp
// WRONG: Shader not compiled yet
glUseProgram(shader);  // ← Will crash

// RIGHT: Compile first
compileShaders();
glUseProgram(shader);  // ← Now safe
```

---

## Key Concepts to Understand

### Normalized Device Coordinates (NDC)

```
Screen Space               NDC Space
(pixels)                   (normalized)

(0, 0)                    (-1, 1) ───────── (1, 1)
  ┌────────────────────┐     │              │
  │   (400, 300)       │     │   (0, 0)     │
  │       ●            │     │     ●        │
  │                    │     │              │
  └────────────────────┘  (-1, -1) ────── (1, -1)
(800, 600)
```

**NDC conversion:**
- X: `pixel_x / (screen_width / 2) - 1` = -1 to +1
- Y: `(screen_height - pixel_y) / (screen_height / 2) - 1` = -1 to +1

**Our triangle vertices in NDC:**
- `(0.0, 0.5)` = center top
- `(-0.5, -0.5)` = lower-left
- `(0.5, -0.5)` = lower-right

### Homogeneous Coordinates

GPU requires 4D positions: `(x, y, z, w)`

```cpp
// Vertex shader transforms 2D → 4D
vec4 gl_Position = vec4(aPos, 0.0, 1.0);
                          ↓      ↓    ↓
                        (x, y)  (z)  (w)
```

**Why `z=0.0, w=1.0`?**
- `z` = depth (0 = neutral, between -1 and +1)
- `w` = perspective division factor (1 = orthographic, no perspective)

For a 2D triangle, this is standard.

### Rasterization

Converting vector geometry (triangles) to raster (pixels).

```
GPU receives:
  gl_Position = (0.5, 0.5, 0.0, 1.0)   // Vertex 1 (top)
  gl_Position = (-0.5, -0.5, 0.0, 1.0) // Vertex 2 (bottom-left)
  gl_Position = (0.5, -0.5, 0.0, 1.0)  // Vertex 3 (bottom-right)

Rasterizer determines:
  Which pixels are inside the triangle?
  
For each pixel inside:
  Execute fragment shader
  Get color from fragColor
  Write to frame buffer
```

---

## Experimentation: Modify & Rebuild

### Change Triangle Color

In `src/main.cpp`, find:
```cpp
fragColor = vec4(0.0, 1.0, 0.5, 1.0);
```

Change to:
```cpp
fragColor = vec4(1.0, 0.0, 0.0, 1.0);  // Red
```

Rebuild:
```bash
~/emsdk/python/3.13.3_64bit/python.exe ~/emsdk/upstream/emscripten/emcc.py \
  src/main.cpp -o build-wasm/dicom_renderer.html -O3 -sUSE_SDL=2 ... -std=c++23
```

Refresh browser. Triangle should be red now!

### Change Triangle Position

Find:
```cpp
float vertices[] = {
    0.0f,  0.5f,
   -0.5f, -0.5f,
    0.5f, -0.5f
};
```

Change to:
```cpp
float vertices[] = {
    0.0f,  0.0f,    // Move to center
   -0.3f, -0.3f,    // Smaller
    0.3f, -0.3f
};
```

Rebuild. Triangle appears smaller and lower.

### Add a Second Triangle

Declare a second VAO/VBO, upload different vertices, draw both:
```cpp
glDrawArrays(GL_TRIANGLES, 0, 3);   // First triangle
glDrawArrays(GL_TRIANGLES, 3, 3);   // Second triangle (vertices 3-5)
```

---

## Summary: Week 2 Checklist

- [ ] **Understand shaders** — GLSL ES 3.00 vertex + fragment
- [ ] **Understand compilation** — Vertex → Fragment → Program
- [ ] **Understand vertex data** — Buffer → VAO → Draw call
- [ ] **Understand NDC** — Normalized coordinates -1 to +1
- [ ] **Understand rasterization** — Triangles → Pixels
- [ ] **Build and run** — See teal triangle in browser
- [ ] **Debug shaders** — Check error logs
- [ ] **Experiment** — Modify colors, positions, add triangles
- [ ] **Understand homogeneous coords** — Why `vec4` not `vec3`

---

## Next Week Preview

Week 3 (G3 — WebGPU) migrates from **OpenGL immediate mode** to **WebGPU deferred mode**:

```
OpenGL (Immediate)          WebGPU (Deferred)
─────────────────           ───────────────────
glUseProgram()    ────────→ wgpuRenderPassSetPipeline()
glBindVertexArray() ──────→ wgpuRenderPassSetBindGroup()
glDrawArrays()    ────────→ wgpuRenderPassDraw()
                           wgpuCommandEncoderFinish()
                           wgpuQueueSubmit()
```

Same triangle, different API. WebGPU is **modern, efficient, and enables compute shaders** (required for DICOM raytracing).

---

## Resources

- **GLSL ES 3.00 Spec:** https://www.khronos.org/registry/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf
- **WebGL2 Spec:** https://www.khronos.org/webgl/specs/
- **OpenGL Tutorial (LearnOpenGL):** https://learnopengl.com/ (excellent educational site)
- **Vertex Attributes & VAO:** https://learnopengl.com/Getting-started/Hello-Triangle
- **Graphics Pipeline:** https://learnopengl.com/Getting-started/Graphics-Pipeline
