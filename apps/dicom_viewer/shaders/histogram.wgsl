// GPU histogram pass.
//
// Why this is WebGPU-only study material:
// WebGL2 can render and sample 3D textures, but it has no compute shaders and no
// storage buffers with atomics. WebGPU gives us exactly those two missing tools:
// one invocation per voxel, and atomicAdd() into shared histogram bins.

struct HistogramParams {
  dims : vec4u,   // .xyz = volume dimensions
  mode : vec4u,   // .x = scalar format, .y = bin count
};

struct Histogram {
  bins : array<atomic<u32>>,
};

@group(0) @binding(0) var volume : texture_3d<f32>;
@group(0) @binding(1) var<storage, read_write> histogram : Histogram;
@group(0) @binding(2) var<uniform> params : HistogramParams;

fn rawScalarAt(coord : vec3u) -> u32 {
  let s = textureLoad(volume, vec3i(coord), 0);

  // Format 1 is the DICOM path: one UInt16 stored value was packed as two
  // normalized bytes in RG8Unorm. textureLoad returns those channels as [0,1],
  // so reconstruct the exact byte values before adding to the histogram.
  if (params.mode.x == 1u) {
    let lo = u32(round(s.r * 255.0));
    let hi = u32(round(s.g * 255.0));
    return lo + (hi << 8u);
  }

  // Format 0 is the R8 phantom/WebGL-style path: already normalized density.
  return u32(round(s.r * f32(params.mode.y - 1u)));
}

@compute @workgroup_size(8, 8, 4)
fn cs_main(@builtin(global_invocation_id) gid : vec3u) {
  if (gid.x >= params.dims.x || gid.y >= params.dims.y || gid.z >= params.dims.z) {
    return;
  }

  let bin = min(rawScalarAt(gid), params.mode.y - 1u);
  atomicAdd(&histogram.bins[bin], 1u);
}
