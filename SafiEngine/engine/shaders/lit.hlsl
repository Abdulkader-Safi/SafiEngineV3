// lit.hlsl — Blinn-Phong lit pipeline for SafiEngine.
//
// Engine-owned shader compiled by engine/CMakeLists.txt into:
//   - lit.vert.spv / lit.frag.spv  (Vulkan / D3D12)
//   - lit.vert.msl / lit.frag.msl  (Metal, via spirv-cross)
//
// SDL_GPU HLSL resource binding contract:
//   vertex shader:
//     b0, space1  → VSUniforms (model, mvp, normal_mat)
//   fragment shader:
//     b0, space2  → CameraBuffer (view, proj, eye_pos)
//     b1, space2  → LightBuffer  (lights[16], ambient, light_count)
//     t0, space2  → base_color texture
//     s0, space2  → base_sampler
//
// Vertex attribute locations match SafiVertex (mesh.h):
//   0 = position (float3)
//   1 = normal   (float3)
//   2 = uv       (float2)

// ---- Light type constants ------------------------------------------------
#define LIGHT_TYPE_DIRECTIONAL 1
#define LIGHT_TYPE_POINT       2
#define LIGHT_TYPE_SPOT        3
#define LIGHT_TYPE_RECT        4

#define MAX_LIGHTS 16

// ---- Structures ----------------------------------------------------------

struct VSInput {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

struct VSOutput {
    float4 clip_pos     : SV_Position;
    float3 world_pos    : TEXCOORD0;
    float3 world_normal : TEXCOORD1;
    float2 uv           : TEXCOORD2;
};

struct GPULight {
    float3 position;
    float  intensity;
    float3 direction;
    float  range;
    float3 color;
    float  inner_angle;
    float  width;
    float  height;
    float  outer_angle;
    uint   type;
};

// ---- Vertex shader resources ---------------------------------------------

cbuffer VSUniforms : register(b0, space1) {
    float4x4 model;
    float4x4 mvp;
    float4x4 normal_mat;   // only upper-left 3x3 used, but padded as 4x4
                            // to avoid HLSL float3x3 packing issues
};

// ---- Fragment shader resources -------------------------------------------

cbuffer CameraBuffer : register(b0, space2) {
    float4x4 cam_view;
    float4x4 cam_proj;
    float3   cam_eye_pos;
    float    _cam_pad;
};

cbuffer LightBuffer : register(b1, space2) {
    GPULight lights[MAX_LIGHTS];
    float3   ambient_color;
    float    ambient_intensity;
    uint     light_count;
    float3   _lb_pad;
};

Texture2D    base_color   : register(t0, space2);
SamplerState base_sampler : register(s0, space2);

// ---- Vertex shader -------------------------------------------------------

VSOutput vs_main(VSInput input) {
    VSOutput o;
    o.clip_pos     = mul(mvp, float4(input.position, 1.0));
    o.world_pos    = mul(model, float4(input.position, 1.0)).xyz;
    o.world_normal = normalize(mul((float3x3)normal_mat, input.normal));
    o.uv           = input.uv;
    return o;
}

// ---- Lighting helpers ----------------------------------------------------

float distance_attenuation(float dist, float range) {
    if (range <= 0.0) return 1.0;
    float ratio = dist / range;
    float ratio2 = ratio * ratio;
    float factor = saturate(1.0 - ratio2);
    return factor * factor;
}

float spot_attenuation(float3 L, float3 spot_dir,
                       float inner_cos, float outer_cos) {
    float cos_angle = dot(-L, spot_dir);
    return smoothstep(outer_cos, inner_cos, cos_angle);
}

// Rect light: representative-point approximation.
// Project the shading point onto the rect's plane, clamp to rect bounds,
// and use that point as the effective light position.
float3 rect_representative_point(float3 frag_pos,
                                  float3 rect_pos,
                                  float3 rect_dir,
                                  float  rect_w,
                                  float  rect_h) {
    // Build a local frame. rect_dir is the normal of the rect.
    float3 up = abs(rect_dir.y) < 0.999
        ? float3(0, 1, 0)
        : float3(1, 0, 0);
    float3 right = normalize(cross(up, rect_dir));
    up = cross(rect_dir, right);

    float3 diff = frag_pos - rect_pos;
    float u = clamp(dot(diff, right), -rect_w * 0.5, rect_w * 0.5);
    float v = clamp(dot(diff, up),    -rect_h * 0.5, rect_h * 0.5);

    return rect_pos + right * u + up * v;
}

// ---- Fragment shader -----------------------------------------------------

static const float SHININESS = 32.0;
static const float SPEC_STRENGTH = 0.5;

float4 fs_main(VSOutput input) : SV_Target0 {
    float3 N = normalize(input.world_normal);
    float3 V = normalize(cam_eye_pos - input.world_pos);
    float4 albedo = base_color.Sample(base_sampler, input.uv);

    float3 result = float3(0, 0, 0);

    for (uint i = 0; i < light_count && i < MAX_LIGHTS; i++) {
        GPULight light = lights[i];
        float3 L;
        float att = 1.0;

        if (light.type == LIGHT_TYPE_DIRECTIONAL) {
            L = -normalize(light.direction);
        }
        else if (light.type == LIGHT_TYPE_POINT) {
            float3 to_light = light.position - input.world_pos;
            float dist = length(to_light);
            L = to_light / max(dist, 0.0001);
            att = distance_attenuation(dist, light.range);
        }
        else if (light.type == LIGHT_TYPE_SPOT) {
            float3 to_light = light.position - input.world_pos;
            float dist = length(to_light);
            L = to_light / max(dist, 0.0001);
            att = distance_attenuation(dist, light.range);
            att *= spot_attenuation(L, normalize(light.direction),
                                    light.inner_angle, light.outer_angle);
        }
        else if (light.type == LIGHT_TYPE_RECT) {
            float3 rep = rect_representative_point(
                input.world_pos, light.position,
                normalize(light.direction),
                light.width, light.height);
            float3 to_light = rep - input.world_pos;
            float dist = length(to_light);
            L = to_light / max(dist, 0.0001);
            // Use diagonal as effective range for rect lights
            float eff_range = length(float2(light.width, light.height)) * 2.0;
            att = distance_attenuation(dist, eff_range);
        }
        else {
            continue;
        }

        // Blinn-Phong
        float NdotL = max(0.0, dot(N, L));
        float3 H = normalize(L + V);
        float NdotH = max(0.0, dot(N, H));
        float spec = pow(NdotH, SHININESS) * SPEC_STRENGTH;

        result += (NdotL + spec) * light.color * light.intensity * att;
    }

    // Ambient from sky light
    result += ambient_color * ambient_intensity;

    return float4(result * albedo.rgb, albedo.a);
}
