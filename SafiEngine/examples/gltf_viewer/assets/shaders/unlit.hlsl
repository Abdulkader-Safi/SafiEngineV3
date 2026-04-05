// unlit.hlsl — minimal unlit textured pipeline for SafiEngine.
// Compiled at runtime by SDL_shadercross → SPIR-V / MSL / DXIL.

// Matches SafiVertex in engine/include/safi/render/mesh.h
struct VSInput {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

struct VSOutput {
    float4 clip_pos : SV_Position;
    float2 uv       : TEXCOORD0;
};

// Uniform buffer — slot 0 for the vertex stage.
cbuffer UBO : register(b0, space1)
{
    float4x4 mvp;
};

VSOutput vs_main(VSInput v)
{
    VSOutput o;
    o.clip_pos = mul(mvp, float4(v.position, 1.0));
    o.uv       = v.uv;
    return o;
}

// Fragment stage: sample the base color texture from slot 0.
Texture2D    base_color : register(t0, space2);
SamplerState base_sampler : register(s0, space2);

float4 fs_main(VSOutput i) : SV_Target0
{
    float4 c = base_color.Sample(base_sampler, i.uv);
    return c;
}
