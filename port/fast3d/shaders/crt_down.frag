#version 450
/* CRT filter ([video] crt), pass 1 (gfx_sdlgpu.cpp crt_present; gfx_opengl.cpp
   has the GL twin): the frame box-filtered down to the virtual screen, the N64's
   240 lines by as many columns as the shape asks, four bilinear taps a pixel,
   and taken to linear light once here so pass 2 needs no pow per tap */
layout(set = 2, binding = 0) uniform sampler2D uSrc;
layout(std140, set = 3, binding = 0) uniform CrtDown {
    vec4 uSize; /* xy: the virtual screen (this pass's target) */
};
layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = gl_FragCoord.xy / uSize.xy;
    vec2 q = 0.25 / uSize.xy;
    vec3 c = texture(uSrc, uv + vec2(-q.x, -q.y)).rgb + texture(uSrc, uv + vec2(q.x, -q.y)).rgb +
             texture(uSrc, uv + vec2(-q.x, q.y)).rgb + texture(uSrc, uv + vec2(q.x, q.y)).rgb;
    outColor = vec4(pow(c * 0.25, vec3(2.2)), 1.0);
}
