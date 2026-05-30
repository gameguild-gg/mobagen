// PASS 1 (ray generation): a fullscreen quad whose fragment shader reconstructs
// a camera ray per pixel from the inverse view-projection (sent from C++ each
// frame) and writes the ray DIRECTION as RGB. WGSL counterpart of raygen.frag.
struct Camera { invViewProj : mat4x4f };
@group(0) @binding(0) var<uniform> cam : Camera;
@group(0) @binding(1) var volume : texture_3d<f32>;
@group(0) @binding(2) var volSamp : sampler;

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

// Ray vs axis-aligned box [-1,1]^3 (slab method).
struct BoxHit { ok : bool, t0 : f32, t1 : f32 };
fn intersectBox(ro : vec3f, rd : vec3f) -> BoxHit {
  let invD = 1.0 / rd;
  let ta = (vec3f(-1.0) - ro) * invD;
  let tb = (vec3f( 1.0) - ro) * invD;
  let tmin = min(ta, tb);
  let tmax = max(ta, tb);
  let t0 = max(max(tmin.x, tmin.y), tmin.z);
  let t1 = min(min(tmax.x, tmax.y), tmax.z);
  var h : BoxHit;
  h.ok = t1 >= max(t0, 0.0);
  h.t0 = t0;
  h.t1 = t1;
  return h;
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

  var col = vec3f(0.04, 0.05, 0.08);        // background

  let hit = intersectBox(ro, rd);
  if (hit.ok) {
    let t0 = max(hit.t0, 0.0);
    let steps = 128;
    let dt = (hit.t1 - t0) / f32(steps);

    var acc = vec4f(0.0);                    // accumulated colour + opacity
    var t = t0;
    for (var i = 0; i < steps; i = i + 1) {
      let p  = ro + rd * t;
      let tc = p * 0.5 + 0.5;                // [-1,1] -> [0,1] texcoords
      let density = textureSampleLevel(volume, volSamp, tc, 0.0).r;

      let a = density * 0.15;                // per-step opacity
      let c = vec3f(density);
      acc = vec4f(acc.rgb + (1.0 - acc.a) * a * c,
                  acc.a   + (1.0 - acc.a) * a);
      if (acc.a > 0.99) { break; }           // early ray termination
      t = t + dt;
    }
    col = mix(col, acc.rgb, acc.a);          // composite over background
  }

  return vec4f(col, 1.0);
}
