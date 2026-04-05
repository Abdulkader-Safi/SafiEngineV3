// unlit.hlsl — minimal unlit textured pipeline for SafiEngine.
//
// Single source of truth for the unlit material. The CMake build
// (cmake/SafiShaders.cmake) compiles this into:
//   - unlit.vert.spv / unlit.frag.spv  (Vulkan / D3D12 SPIR-V path)
//   - unlit.vert.msl / unlit.frag.msl  (Metal, via spirv-cross)
//
// SDL_GPU HLSL resource binding contract (must match safi_material_create_unlit):
//   vertex shader:
//     b0, space1  → uniform buffer slot 0   (mvp)
//   fragment shader:
//     t0, space2  → sampled texture slot 0  (base color)
//     s0, space2  → sampler slot 0          (base sampler)
//
// Vertex attribute locations match SafiVertex (mesh.h):
//   0 = position (float3)
//   1 = normal   (float3)
//   2 = uv       (float2)

struct VSInput {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

cbuffer VSUniforms : register(b0, space1) {
    float4x4 mvp;
};

VSOutput vs_main(VSInput input) {
    VSOutput o;
    o.position = mul(mvp, float4(input.position, 1.0));
    o.uv       = input.uv;
    return o;
}

Texture2D    base_color   : register(t0, space2);
SamplerState base_sampler : register(s0, space2);

float4 fs_main(VSOutput input) : SV_Target0 {
    return base_color.Sample(base_sampler, input.uv);
}
