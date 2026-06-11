// PASS 2 (blit): sample the offscreen ray-gen texture onto a fullscreen quad,
// multiplied by a tint (the 1-4 buttons). WGSL counterpart of blit.frag.
@group(0) @binding(0) var sceneTex : texture_2d<f32>;
@group(0) @binding(1) var sceneSamp : sampler;
@group(0) @binding(2) var<uniform> tint : vec4f;

struct VsOut {
  @builtin(position) pos : vec4f,
  @location(0) uv : vec2f,
};

@vertex
fn vs_post(@location(0) position : vec2f, @location(1) uv : vec2f) -> VsOut {
  var out : VsOut;
  out.pos = vec4f(position, 0.0, 1.0);
  out.uv = uv;
  return out;
}

@fragment
fn fs_post(@location(0) uv : vec2f) -> @location(0) vec4f {
  return textureSample(sceneTex, sceneSamp, uv) * tint;
}
