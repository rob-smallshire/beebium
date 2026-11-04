#version 440

// The picture quad: a unit quad scaled to the aspect-fitted rectangle
// (client specification 6.2). Uniform block mirrors DisplayUniforms.

layout(location = 0) out vec2 v_uv;

layout(std140, binding = 0) uniform buf {
    vec4 quad;
    vec4 texture_size;
    ivec4 region_info;
    ivec4 regions[8];
    vec4 inner;
    vec4 band_colour;
};

void main() {
    // Triangle strip: bottom-left, bottom-right, top-left, top-right.
    vec2 corner = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    // Texture v runs from the top row (0) to the bottom row (1).
    v_uv = vec2(corner.x, 1.0 - corner.y);
    vec2 position = (corner * 2.0 - 1.0) * quad.xy;
    gl_Position = vec4(position.x, position.y * quad.z, 0.0, 1.0);
}
