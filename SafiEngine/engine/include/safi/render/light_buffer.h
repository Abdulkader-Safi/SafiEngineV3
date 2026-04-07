/**
 * safi/render/light_buffer.h — GPU-side light and camera uniform structs.
 *
 * These C structs mirror the HLSL cbuffer layouts in lit.hlsl exactly.
 * They are packed to 16-byte boundaries to satisfy GPU uniform alignment.
 */
#ifndef SAFI_RENDER_LIGHT_BUFFER_H
#define SAFI_RENDER_LIGHT_BUFFER_H

#include <stdint.h>
#include <string.h>
#include <cglm/cglm.h>

#define SAFI_MAX_LIGHTS 16

#define SAFI_LIGHT_TYPE_NONE        0
#define SAFI_LIGHT_TYPE_DIRECTIONAL 1
#define SAFI_LIGHT_TYPE_POINT       2
#define SAFI_LIGHT_TYPE_SPOT        3
#define SAFI_LIGHT_TYPE_RECT        4
#define SAFI_LIGHT_TYPE_SKY         5

/* Per-light GPU data. 64 bytes (4 × float4), naturally aligned. */
typedef struct SafiGPULight {
    float    position[3];
    float    intensity;
    float    direction[3];
    float    range;
    float    color[3];
    float    inner_angle;
    float    width;
    float    height;
    float    outer_angle;
    uint32_t type;
} SafiGPULight;

/* Matches cbuffer LightBuffer in lit.hlsl. */
typedef struct SafiLightBuffer {
    SafiGPULight lights[SAFI_MAX_LIGHTS];
    float        ambient_color[3];
    float        ambient_intensity;
    uint32_t     light_count;
    float        _pad[3];
} SafiLightBuffer;

/* Matches cbuffer CameraBuffer in lit.hlsl. */
typedef struct SafiCameraBuffer {
    float view[16];
    float proj[16];
    float eye_pos[3];
    float _pad;
} SafiCameraBuffer;

/* Matches cbuffer VSUniforms in lit.hlsl.
 * normal_mat is stored as float4x4 (only upper-left 3×3 used) to avoid
 * HLSL float3x3 packing issues across backends. */
typedef struct SafiLitVSUniforms {
    float model[16];
    float mvp[16];
    float normal_mat[16];   /* float4x4, only 3×3 portion used in shader */
} SafiLitVSUniforms;

/* Compute the normal matrix (inverse-transpose of upper-left 3×3 of model)
 * and store as a full 4×4 matrix (shader only reads upper-left 3×3). */
static inline void safi_compute_normal_matrix(const float *model4x4,
                                              float       *out16) {
    mat4 inv;
    glm_mat4_inv((vec4 *)model4x4, inv);
    /* Transpose of inverse: swap rows and columns */
    mat4 inv_t;
    glm_mat4_transpose_to(inv, inv_t);
    memcpy(out16, inv_t, 64);
}

#endif /* SAFI_RENDER_LIGHT_BUFFER_H */
