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

@fragment
fn fs_main(@location(0) uv : vec2f) -> @location(0) vec4f {
  let ndc = uv * 2.0 - 1.0;
  // invViewProj is built in C++ with glm (OpenGL depth convention, z in [-1,1]),
  // so we unproject near z=-1 and far z=+1 here too. This is pure matrix math —
  // independent of WebGPU's own clip-space depth range.
  let nearH = cam.invViewProj * vec4f(ndc, -1.0, 1.0);
  let farH  = cam.invViewProj * vec4f(ndc,  1.0, 1.0);
  let nearP = nearH.xyz / nearH.w;
  let farP  = farH.xyz / farH.w;
  let rayDir = normalize(farP - nearP);
  return vec4f(rayDir * 0.5 + 0.5, 1.0);
}
