#version 440

// Samples the frame. The texture holds the server's BGRA32 bytes uploaded as
// RGBA8, so the channels are swizzled back here; one code path serves Vulkan
// and GLES, which only has BGRA textures by extension (spec 6.1). Each
// scanline's split-screen region stretches its logical width across the
// display width (spec 6.2).

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_colour;

layout(std140, binding = 0) uniform buf {
    vec4 quad;
    vec4 texture_size;
    ivec4 region_info;
    ivec4 regions[8];
    vec4 inner;
    vec4 band_colour;
};

layout(binding = 1) uniform sampler2D frame_texture;

void main() {
    // Outside the active picture, within the quad: the edge margin.
    if (v_uv.x < inner.x || v_uv.x >= inner.z || v_uv.y < inner.y || v_uv.y >= inner.w) {
        frag_colour = band_colour;
        return;
    }
    vec2 uv = (v_uv - inner.xy) / (inner.zw - inner.xy);
    float u = uv.x;
    if (region_info.x > 1) {
        int scanline = clamp(int(uv.y * texture_size.y), 0, int(texture_size.y) - 1);
        float pixel_width = texture_size.x;
        for (int i = 0; i < 8; ++i) {
            if (i >= region_info.x) {
                break;
            }
            if (scanline >= regions[i].x && scanline < regions[i].y) {
                pixel_width = float(regions[i].z);
                break;
            }
        }
        u = uv.x * pixel_width / texture_size.x;
    }
    vec4 texel = texture(frame_texture, vec2(u, uv.y));
    frag_colour = vec4(texel.b, texel.g, texel.r, 1.0);
}
