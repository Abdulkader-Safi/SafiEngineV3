// nuklear.hlsl — Nuklear debug-UI pipeline shader.
//
// Cross-platform single source compiled by cmake/SafiShaders.cmake into:
//   nuklear.vert.spv / nuklear.frag.spv  (Vulkan / SPIR-V path)
//   nuklear.vert.msl / nuklear.frag.msl  (Metal, via spirv-cross)
//
// SDL_GPU HLSL resource binding contract (must match s_create_pipeline in
// debug_ui.c — 0 samplers + 1 UBO on vertex stage, 1 sampler on fragment):
//   vertex shader:
//     b0, space1  → uniform buffer slot 0  (inv_half_viewport)
//   fragment shader:
//     t0, space2  → font atlas texture
//     s0, space2  → font atlas sampler
//
// Vertex attribute locations match SafiNkVertex in debug_ui.c:
//   0 = position (float2, screen-space pixels)
//   1 = uv       (float2)
//   2 = color    (unorm4)

struct VSInput {
    float2 position : TEXCOORD0;
    float2 uv       : TEXCOORD1;
    float4 color    : TEXCOORD2;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

cbuffer NkUBO : register(b0, space1) {
    float2 inv_half_viewport;   // = { 2.0/width, 2.0/height }
};

VSOutput nk_vs(VSInput input) {
    VSOutput o;
    float2 ndc;
    ndc.x =  input.position.x * inv_half_viewport.x - 1.0;
    ndc.y = -(input.position.y * inv_half_viewport.y - 1.0);
    o.position = float4(ndc, 0.0, 1.0);
    o.uv       = input.uv;
    o.color    = input.color;
    return o;
}

Texture2D    nk_tex : register(t0, space2);
SamplerState nk_smp : register(s0, space2);

float4 nk_fs(VSOutput input) : SV_Target0 {
    return input.color * nk_tex.Sample(nk_smp, input.uv);
}
