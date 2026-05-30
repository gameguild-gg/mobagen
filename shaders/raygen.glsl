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
// For each pixel, reconstruct a camera ray from the inverse view-projection and
// output its DIRECTION as RGB. In Tier 2.2+ main() starts marching this ray.
in vec2 vUv;
out vec4 fragColor;

uniform mat4 inv_view_projection;

void main() {
    vec2 ndc = vUv * 2.0 - 1.0;                       // pixel -> clip space xy

    vec4 nearH = inv_view_projection * vec4(ndc, -1.0, 1.0);
    vec4 farH  = inv_view_projection * vec4(ndc,  1.0, 1.0);
    vec3 nearP = nearH.xyz / nearH.w;                 // world point on near plane
    vec3 farP  = farH.xyz / farH.w;                   // world point on far plane

    vec3 rayDir = normalize(farP - nearP);            // the ray for this pixel

    // Visualize direction: map [-1,1] -> [0,1]. Orbit the camera and watch the
    // gradient swing — that proves the rays track the camera correctly.
    fragColor = vec4(rayDir * 0.5 + 0.5, 1.0);
}
#endif
