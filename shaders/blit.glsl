// Pass-2 blit program. One .glsl file, both stages (see raygen.glsl for how the
// loader compiles it). Samples the offscreen render texture onto a fullscreen
// quad, multiplied by a tint — the "deliver the render-texture to the screen" step.

#ifdef VERTEX_SHADER
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;

out vec2 vUv;

void main() {
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uScene;   // the texture rendered in pass 1
uniform vec4 uTint;

void main() {
    fragColor = texture(uScene, vUv) * uTint;
}
#endif
