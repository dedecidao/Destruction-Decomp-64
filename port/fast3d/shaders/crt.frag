#version 450
/* CRT filter ([video] crt), pass 2 (gfx_sdlgpu.cpp crt_present; gfx_opengl.cpp
   has the GL twin): the virtual screen of pass 1 drawn as a tube TV would show
   it. Adapted from Timothy Lottes' public-domain CRT shader (libretro
   glsl-shaders, crt/shaders/crt-lottes.glsl), cut down for phones:
   - each of the 240 lines is a beam with a gaussian profile (hardScan), the
     three nearest lines lighting a pixel, and a bright line's beam is wider
     (the light bloom; uLook.z);
   - along a line, a 3-tap gaussian (hardPix) between the columns;
   - a soft glow from four bilinear taps around the pixel;
   - an aperture grille in screen pixels: R G B stripes, or green / magenta
     where the screen has too few pixels for three;
   - the optional curvature is Lottes' Warp. */
layout(set = 2, binding = 0) uniform sampler2D uLines; /* pass 1's target, linear light */
layout(std140, set = 3, binding = 0) uniform Crt {
    vec4 uView; /* x: the viewport's left edge, y: its width, z: its height, w: 1 = flip vertically */
    vec4 uVirt; /* xy: the virtual screen's size, z: curvature (0 = flat), w: mask strength */
    vec4 uLook; /* x: hardScan, y: hardPix, z: bloom, w: brightness */
    vec4 uMask; /* x: the grille's period in pixels, 2 or 3 */
};
layout(location = 0) out vec4 outColor;

vec3 fetchLine(ivec2 p) {
    return texelFetch(uLines, clamp(p, ivec2(0), ivec2(uVirt.xy) - ivec2(1)), 0).rgb;
}

void main() {
    vec2 uv = vec2((gl_FragCoord.x - uView.x) / uView.y, gl_FragCoord.y / uView.z);
    if (uView.w > 0.5) {
        uv.y = 1.0 - uv.y;
    }
    if (uVirt.z > 0.0) {
        vec2 p = uv * 2.0 - 1.0;
        p *= vec2(1.0 + p.y * p.y * 0.031 * uVirt.z, 1.0 + p.x * p.x * 0.041 * uVirt.z);
        uv = p * 0.5 + 0.5;
        if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0) {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }
    vec2 pos = uv * uVirt.xy;
    vec2 cell = floor(pos);
    vec2 d = pos - (cell + 0.5);
    ivec2 c = ivec2(cell);

    float wl = exp2(uLook.y * (d.x + 1.0) * (d.x + 1.0));
    float wc = exp2(uLook.y * d.x * d.x);
    float wr = exp2(uLook.y * (d.x - 1.0) * (d.x - 1.0));
    float norm = 1.0 / (wl + wc + wr);
    vec3 acc = vec3(0.0);
    for (int k = -1; k <= 1; k++) {
        vec3 line = (fetchLine(c + ivec2(-1, k)) * wl + fetchLine(c + ivec2(0, k)) * wc +
                     fetchLine(c + ivec2(1, k)) * wr) * norm;
        float lum = clamp(dot(line, vec3(0.30, 0.59, 0.11)), 0.0, 1.0);
        float dy = d.y - float(k);
        acc += line * exp2(uLook.x * (1.0 - uLook.z * 0.55 * lum) * dy * dy);
    }
    vec2 g = 1.0 / uVirt.xy;
    vec3 glow = texture(uLines, (pos + vec2(-2.0, -1.5)) * g).rgb + texture(uLines, (pos + vec2(2.0, -1.5)) * g).rgb +
                texture(uLines, (pos + vec2(-2.0, 1.5)) * g).rgb + texture(uLines, (pos + vec2(2.0, 1.5)) * g).rgb;
    acc += glow * (0.03 * uLook.z);

    float m = uVirt.w;
    vec3 mask = vec3(1.0 - m);
    float px = mod(floor(gl_FragCoord.x), uMask.x);
    if (uMask.x > 2.5) {
        if (px < 0.5) {
            mask.r = 1.0 + m;
        } else if (px < 1.5) {
            mask.g = 1.0 + m;
        } else {
            mask.b = 1.0 + m;
        }
    } else if (px < 0.5) {
        mask = vec3(1.0 - m, 1.0 + m, 1.0 - m);
    } else {
        mask = vec3(1.0 + m, 1.0 - m, 1.0 + m);
    }
    outColor = vec4(pow(clamp(acc * mask * uLook.w, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
