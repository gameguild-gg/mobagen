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
uniform int uMode;             // 0 = DVR, 1 = MIP, 2 = Isosurface

const vec3 LIGHT_DIR = vec3(0.6, 0.8, 0.5);

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

// Light a colour by the density gradient at tc (ambient + diffuse).
vec3 shade(vec3 tc, vec3 base) {
    vec3 grad = volumeGradient(tc);
    float gmag = length(grad);
    float light = 0.3;                            // ambient
    if (gmag > 0.001) {
        vec3 n = -grad / gmag;
        light += 0.7 * max(dot(n, normalize(LIGHT_DIR)), 0.0);
    }
    return base * light;
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

        if (uMode == 1) {
            // --- MIP: brightest density along the ray (e.g. angiography) ---
            float maxD = 0.0;
            float t = t0;
            for (int i = 0; i < STEPS; i++) {
                vec3 tc = (ro + rd * t) * 0.5 + 0.5;
                maxD = max(maxD, texture(uVolume, tc).r);
                t += dt;
            }
            col = texture(uTransfer, vec2(maxD, 0.5)).rgb;
        } else if (uMode == 2) {
            // --- Isosurface: stop at the first density above a threshold ---
            const float ISO = 0.40;
            float t = t0;
            for (int i = 0; i < STEPS; i++) {
                vec3 tc = (ro + rd * t) * 0.5 + 0.5;
                float density = texture(uVolume, tc).r;
                if (density > ISO) {
                    vec3 base = texture(uTransfer, vec2(density, 0.5)).rgb;
                    col = shade(tc, base);
                    break;
                }
                t += dt;
            }
        } else {
            // --- DVR: accumulate colour + opacity front-to-back ---
            vec4 acc = vec4(0.0);
            float t = t0;
            for (int i = 0; i < STEPS; i++) {
                vec3 tc = (ro + rd * t) * 0.5 + 0.5;
                float density = texture(uVolume, tc).r;
                vec4 tf = texture(uTransfer, vec2(density, 0.5));
                float a = tf.a * 0.2;                  // per-step opacity
                vec3  c = shade(tc, tf.rgb);
                acc.rgb += (1.0 - acc.a) * a * c;
                acc.a   += (1.0 - acc.a) * a;
                if (acc.a > 0.99) break;               // early ray termination
                t += dt;
            }
            col = mix(col, acc.rgb, acc.a);
        }
    }

    fragColor = vec4(col, 1.0);
}
#endif
