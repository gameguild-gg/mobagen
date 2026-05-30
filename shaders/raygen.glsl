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
// Tier 2.2: march each camera ray through space and hit-test an implicit sphere
// via its signed distance function. Same loop volume rendering uses — in Tier 2.3
// sdfSphere() becomes a 3D-texture lookup and "stop on hit" becomes "accumulate".
in vec2 vUv;
out vec4 fragColor;

uniform mat4 inv_view_projection;

// Signed distance to a sphere at the origin: >0 outside, 0 on surface, <0 inside.
float sdfSphere(vec3 p) {
    return length(p) - 0.7;
}

// Surface normal = gradient of the SDF, via central differences (6 samples).
vec3 calcNormal(vec3 p) {
    float e = 0.001;
    return normalize(vec3(
        sdfSphere(p + vec3(e, 0.0, 0.0)) - sdfSphere(p - vec3(e, 0.0, 0.0)),
        sdfSphere(p + vec3(0.0, e, 0.0)) - sdfSphere(p - vec3(0.0, e, 0.0)),
        sdfSphere(p + vec3(0.0, 0.0, e)) - sdfSphere(p - vec3(0.0, 0.0, e))));
}

void main() {
    vec2 ndc = vUv * 2.0 - 1.0;                       // pixel -> clip space xy

    vec4 nearH = inv_view_projection * vec4(ndc, -1.0, 1.0);
    vec4 farH  = inv_view_projection * vec4(ndc,  1.0, 1.0);
    vec3 ro = nearH.xyz / nearH.w;                    // ray origin (near plane)
    vec3 farP = farH.xyz / farH.w;
    vec3 rd = normalize(farP - ro);                   // ray direction

    // Sphere tracing: step by the SDF (the largest safe distance) until we
    // touch the surface (d < EPS) or the ray leaves the scene (t > MAX_DIST).
    const int   MAX_STEPS = 96;
    const float MAX_DIST  = 20.0;
    const float EPS       = 0.001;

    float t = 0.0;
    bool hit = false;
    for (int i = 0; i < MAX_STEPS; i++) {
        vec3 p = ro + rd * t;
        float d = sdfSphere(p);
        if (d < EPS) { hit = true; break; }
        t += d;
        if (t > MAX_DIST) break;
    }

    vec3 col = vec3(0.04, 0.05, 0.08);                // background
    if (hit) {
        vec3 p = ro + rd * t;
        vec3 n = calcNormal(p);
        vec3 lightDir = normalize(vec3(0.6, 0.8, 0.5));
        float diff = max(dot(n, lightDir), 0.0);
        col = vec3(0.2, 0.5, 1.0) * (0.15 + 0.85 * diff);   // ambient + diffuse
    }
    fragColor = vec4(col, 1.0);
}
#endif
