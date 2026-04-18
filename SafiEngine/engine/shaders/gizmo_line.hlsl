// gizmo_line.hlsl — editor gizmo line-list pipeline.
//
// Renders world-space lines with per-vertex RGBA colours. Used for wireframe
// overlays (selection AABBs, translate/rotate/scale handles, debug draws).
//
// SDL_GPU HLSL resource binding contract (must match s_create_pipeline in
// engine/src/render/gizmo.c — 0 samplers + 1 UBO on vertex stage, 0 on fragment):
//   vertex shader:
//     b0, space1  → cbuffer GizmoVSUBO { float4x4 view_proj; }
//
// Vertex attribute locations match SafiGizmoVertex in gizmo.c:
//   0 = position (float3, world-space)
//   1 = color    (float4, linear RGBA 0..1)

struct VSInput {
    float3 position : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

struct VSOutput {
    float4 clip_pos : SV_Position;
    float4 color    : TEXCOORD0;
};

cbuffer GizmoVSUBO : register(b0, space1) {
    float4x4 view_proj;
};

VSOutput gizmo_vs(VSInput input) {
    VSOutput o;
    o.clip_pos = mul(view_proj, float4(input.position, 1.0));
    o.color    = input.color;
    return o;
}

float4 gizmo_fs(VSOutput input) : SV_Target0 {
    return input.color;
}
