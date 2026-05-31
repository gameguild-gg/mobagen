// Ray-generation program (Tier 2.1). One .glsl file holds BOTH stages, like a
// .wgsl module holds both entry points. The C++ loader compiles this twice —
// once with VERTEX_SHADER defined, once with FRAGMENT_SHADER — and prepends the
// right #version / precision header so the same source serves WebGL2/GLES3 and
// desktop GL 3.3.

#ifdef VERTEX_SHADER
// Fullscreen quad: positions are already NDC; pass uv = (pos+1)/2 downstream.
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;

out vec2 vUv;

void main() {
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
// Tier 2.3: Direct Volume Rendering. March each camera ray through a 3D texture
// that lives in the unit cube [-1,1]^3, sampling density and ACCUMULATING colour
// + opacity front-to-back. This is the same loop as Tier 2.2, but it samples a
// texture instead of an SDF and blends instead of stopping. Tier 3 swaps the
// synthetic volume for DICOM data; Tier 2.4 adds a real transfer function.
in vec2 vUv;
out vec4 fragColor;

uniform mat4 inv_view_projection;
uniform sampler3D uVolume;
uniform sampler2D uTransfer;   // 1D transfer LUT (256x1): density -> RGBA

// Volume "normal" = gradient of the density field (central differences over one
// voxel). Points toward INCREASING density; we negate it for an outward normal.
vec3 volumeGradient(vec3 tc) {
    float h = 1.0 / 64.0;   // one voxel step in texture coords (volume is 64^3)
    return vec3(
        texture(uVolume, tc + vec3(h, 0.0, 0.0)).r - texture(uVolume, tc - vec3(h, 0.0, 0.0)).r,
        texture(uVolume, tc + vec3(0.0, h, 0.0)).r - texture(uVolume, tc - vec3(0.0, h, 0.0)).r,
        texture(uVolume, tc + vec3(0.0, 0.0, h)).r - texture(uVolume, tc - vec3(0.0, 0.0, h)).r);
}

// Ray vs axis-aligned box [-1,1]^3 (slab method). Returns entry/exit distances.
bool intersectBox(vec3 ro, vec3 rd, out float t0, out float t1) {
    vec3 invD = 1.0 / rd;
    vec3 ta = (vec3(-1.0) - ro) * invD;
    vec3 tb = (vec3( 1.0) - ro) * invD;
    vec3 tmin = min(ta, tb);
    vec3 tmax = max(ta, tb);
    t0 = max(max(tmin.x, tmin.y), tmin.z);
    t1 = min(min(tmax.x, tmax.y), tmax.z);
    return t1 >= max(t0, 0.0);
}

void main() {
    vec2 ndc = vUv * 2.0 - 1.0;                       // pixel -> clip space xy

    vec4 nearH = inv_view_projection * vec4(ndc, -1.0, 1.0);
    vec4 farH  = inv_view_projection * vec4(ndc,  1.0, 1.0);
    vec3 ro = nearH.xyz / nearH.w;                    // ray origin (near plane)
    vec3 farP = farH.xyz / farH.w;
    vec3 rd = normalize(farP - ro);                   // ray direction

    vec3 col = vec3(0.04, 0.05, 0.08);                // background

    float t0, t1;
    if (intersectBox(ro, rd, t0, t1)) {
        t0 = max(t0, 0.0);                            // start at the box / camera
        const int STEPS = 128;
        float dt = (t1 - t0) / float(STEPS);

        vec4 acc = vec4(0.0);                         // accumulated colour + opacity
        float t = t0;
        for (int i = 0; i < STEPS; i++) {
            vec3 p  = ro + rd * t;
            vec3 tc = p * 0.5 + 0.5;                  // [-1,1] -> [0,1] texcoords
            float density = texture(uVolume, tc).r;

            // Transfer function: map density -> colour + opacity via a 1D LUT.
            // This is the knob that turns gray fog into selected structure.
            vec4 tf = texture(uTransfer, vec2(density, 0.5));
            float a = tf.a * 0.2;                      // per-step opacity
            vec3  c = tf.rgb;

            // Gradient shading: light the sample by the density gradient so the
            // volume reads as a 3D solid instead of flat fog. Flat regions
            // (tiny gradient) stay unlit (ambient only).
            vec3 grad = volumeGradient(tc);
            float gmag = length(grad);
            if (gmag > 0.001) {
                vec3 n = -grad / gmag;
                float diff = max(dot(n, normalize(vec3(0.6, 0.8, 0.5))), 0.0);
                c *= (0.3 + 0.7 * diff);              // ambient + diffuse
            }

            acc.rgb += (1.0 - acc.a) * a * c;         // front-to-back compositing
            acc.a   += (1.0 - acc.a) * a;
            if (acc.a > 0.99) break;                  // early ray termination
            t += dt;
        }
        col = mix(col, acc.rgb, acc.a);               // composite over background
    }

    fragColor = vec4(col, 1.0);
}
#endif
