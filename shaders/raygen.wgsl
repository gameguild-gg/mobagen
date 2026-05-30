// PASS 1 (ray generation): a fullscreen quad whose fragment shader reconstructs
// a camera ray per pixel from the inverse view-projection (sent from C++ each
// frame) and writes the ray DIRECTION as RGB. WGSL counterpart of raygen.frag.
struct Camera { invViewProj : mat4x4f };
@group(0) @binding(0) var<uniform> cam : Camera;

struct VsOut {
  @builtin(position) pos : vec4f,
  @location(0) uv : vec2f,
};

@vertex
fn vs_main(@location(0) position : vec2f, @location(1) uv : vec2f) -> VsOut {
  var out : VsOut;
  out.pos = vec4f(position, 0.0, 1.0);
  out.uv = uv;
  return out;
}

// Signed distance to a sphere at the origin: >0 outside, 0 on surface, <0 inside.
fn sdfSphere(p : vec3f) -> f32 {
  return length(p) - 0.7;
}

// Surface normal = gradient of the SDF, via central differences (6 samples).
fn calcNormal(p : vec3f) -> vec3f {
  let e = 0.001;
  return normalize(vec3f(
    sdfSphere(p + vec3f(e, 0.0, 0.0)) - sdfSphere(p - vec3f(e, 0.0, 0.0)),
    sdfSphere(p + vec3f(0.0, e, 0.0)) - sdfSphere(p - vec3f(0.0, e, 0.0)),
    sdfSphere(p + vec3f(0.0, 0.0, e)) - sdfSphere(p - vec3f(0.0, 0.0, e))));
}

@fragment
fn fs_main(@location(0) uv : vec2f) -> @location(0) vec4f {
  let ndc = uv * 2.0 - 1.0;
  // invViewProj is built in C++ with glm (OpenGL depth convention, z in [-1,1]),
  // so we unproject near z=-1 and far z=+1 here too — pure matrix math.
  let nearH = cam.invViewProj * vec4f(ndc, -1.0, 1.0);
  let farH  = cam.invViewProj * vec4f(ndc,  1.0, 1.0);
  let ro = nearH.xyz / nearH.w;             // ray origin (near plane)
  let farP = farH.xyz / farH.w;
  let rd = normalize(farP - ro);            // ray direction

  // Sphere tracing: step by the SDF until we hit the surface or leave the scene.
  var t = 0.0;
  var hit = false;
  for (var i = 0; i < 96; i = i + 1) {
    let p = ro + rd * t;
    let d = sdfSphere(p);
    if (d < 0.001) { hit = true; break; }
    t = t + d;
    if (t > 20.0) { break; }
  }

  var col = vec3f(0.04, 0.05, 0.08);        // background
  if (hit) {
    let p = ro + rd * t;
    let n = calcNormal(p);
    let lightDir = normalize(vec3f(0.6, 0.8, 0.5));
    let diff = max(dot(n, lightDir), 0.0);
    col = vec3f(0.2, 0.5, 1.0) * (0.15 + 0.85 * diff);   // ambient + diffuse
  }
  return vec4f(col, 1.0);
}
