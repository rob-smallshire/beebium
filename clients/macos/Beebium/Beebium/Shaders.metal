#include <metal_stdlib>
using namespace metal;

// Vertex data for a full-screen quad
struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
};

// Full-screen quad vertices (two triangles)
constant float2 quadVertices[] = {
    float2(-1, -1), // bottom-left
    float2( 1, -1), // bottom-right
    float2(-1,  1), // top-left
    float2( 1, -1), // bottom-right
    float2( 1,  1), // top-right
    float2(-1,  1), // top-left
};

// Texture coordinates for the quad
constant float2 quadTexCoords[] = {
    float2(0, 1), // bottom-left (note: texture Y is flipped)
    float2(1, 1), // bottom-right
    float2(0, 0), // top-left
    float2(1, 1), // bottom-right
    float2(1, 0), // top-right
    float2(0, 0), // top-left
};

// Vertex shader: output full-screen quad
vertex VertexOut vertexShader(uint vertexID [[vertex_id]]) {
    VertexOut out;
    out.position = float4(quadVertices[vertexID], 0, 1);
    out.texCoord = quadTexCoords[vertexID];
    return out;
}

// Fragment shader: sample the emulator framebuffer texture
fragment float4 fragmentShader(VertexOut in [[stage_in]],
                                texture2d<float> texture [[texture(0)]]) {
    constexpr sampler textureSampler(mag_filter::nearest,
                                      min_filter::nearest,
                                      address::clamp_to_edge);
    return texture.sample(textureSampler, in.texCoord);
}
