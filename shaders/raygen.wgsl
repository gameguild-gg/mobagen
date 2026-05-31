// PASS 1 (ray generation): a fullscreen quad whose fragment shader reconstructs
// a camera ray per pixel from the inverse view-projection (sent from C++ each
// frame) and writes the ray DIRECTION as RGB. WGSL counterpart of raygen.frag.
struct Camera { invViewProj : mat4x4f };
@group(0) @binding(0) var<uniform> cam : Camera;
@group(0) @binding(1) var volume : texture_3d<f32>;
@group(0) @binding(2) var volSamp : sampler;
@group(0) @binding(3) var transferTex : texture_2d<f32>;  // 1D LUT: density -> RGBA
@group(0) @binding(4) var<uniform> uMode : vec4<u32>;     // .x: 0=DVR 1=MIP 2=Iso
@group(0) @binding(5) var<uniform> uWindow : vec4f;       // .x center, .y width
@group(0) @binding(6) var<uniform> uBoxHalf : vec4f;      // .xyz box half-extents

// Window/level: remap [center-width/2, center+width/2] to [0,1], clip outside.
fn applyWindow(v : f32) -> f32 {
  let lo = uWindow.x - uWindow.y * 0.5;
  return clamp((v - lo) / uWindow.y, 0.0, 1.0);
}

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

// Volume "normal" = gradient of the density field (central differences over one
// voxel), pointing toward increasing density.
fn volumeGradient(tc : vec3f) -> vec3f {
  let h = 1.0 / f32(textureDimensions(volume).x);   // one voxel step
  return vec3f(
    textureSampleLevel(volume, volSamp, tc + vec3f(h, 0.0, 0.0), 0.0).r - textureSampleLevel(volume, volSamp, tc - vec3f(h, 0.0, 0.0), 0.0).r,
    textureSampleLevel(volume, volSamp, tc + vec3f(0.0, h, 0.0), 0.0).r - textureSampleLevel(volume, volSamp, tc - vec3f(0.0, h, 0.0), 0.0).r,
    textureSampleLevel(volume, volSamp, tc + vec3f(0.0, 0.0, h), 0.0).r - textureSampleLevel(volume, volSamp, tc - vec3f(0.0, 0.0, h), 0.0).r);
}

// Ray vs axis-aligned box [-1,1]^3 (slab method).
struct BoxHit { ok : bool, t0 : f32, t1 : f32 };
fn intersectBox(ro : vec3f, rd : vec3f) -> BoxHit {
  let invD = 1.0 / rd;
  let ta = (-uBoxHalf.xyz - ro) * invD;
  let tb = ( uBoxHalf.xyz - ro) * invD;
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

// Light a colour by the density gradient at tc (ambient + diffuse).
fn shade(tc : vec3f, base : vec3f) -> vec3f {
  let grad = volumeGradient(tc);
  let gmag = length(grad);
  var light = 0.3;                           // ambient
  if (gmag > 0.001) {
    let n = -grad / gmag;
    light = light + 0.7 * max(dot(n, normalize(vec3f(0.6, 0.8, 0.5))), 0.0);
  }
  return base * light;
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

    if (uMode.x == 1u) {
      // --- MIP: brightest density along the ray ---
      var maxD = 0.0;
      var t = t0;
      for (var i = 0; i < steps; i = i + 1) {
        let tc = (ro + rd * t) / uBoxHalf.xyz * 0.5 + 0.5;
        maxD = max(maxD, applyWindow(textureSampleLevel(volume, volSamp, tc, 0.0).r));
        t = t + dt;
      }
      col = textureSampleLevel(transferTex, volSamp, vec2f(maxD, 0.5), 0.0).rgb;
    } else if (uMode.x == 2u) {
      // --- Isosurface: first density above a threshold ---
      let ISO = 0.40;
      var t = t0;
      for (var i = 0; i < steps; i = i + 1) {
        let tc = (ro + rd * t) / uBoxHalf.xyz * 0.5 + 0.5;
        let density = applyWindow(textureSampleLevel(volume, volSamp, tc, 0.0).r);
        if (density > ISO) {
          let base = textureSampleLevel(transferTex, volSamp, vec2f(density, 0.5), 0.0).rgb;
          col = shade(tc, base);
          break;
        }
        t = t + dt;
      }
    } else {
      // --- DVR: accumulate colour + opacity front-to-back ---
      var acc = vec4f(0.0);
      var t = t0;
      for (var i = 0; i < steps; i = i + 1) {
        let tc = (ro + rd * t) / uBoxHalf.xyz * 0.5 + 0.5;
        let density = applyWindow(textureSampleLevel(volume, volSamp, tc, 0.0).r);
        let tf = textureSampleLevel(transferTex, volSamp, vec2f(density, 0.5), 0.0);
        let a = tf.a * 0.2;
        let c = shade(tc, tf.rgb);
        acc = vec4f(acc.rgb + (1.0 - acc.a) * a * c,
                    acc.a   + (1.0 - acc.a) * a);
        if (acc.a > 0.99) { break; }
        t = t + dt;
      }
      col = mix(col, acc.rgb, acc.a);
    }
  }

  return vec4f(col, 1.0);
}
