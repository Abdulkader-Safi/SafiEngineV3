/**
 * safi/render/material.h — pipeline + shader + base-color texture.
 *
 * The first-pass engine supports a single "unlit textured" material. It owns
 * the graphics pipeline, a sampler, and a GPUTexture for the base color.
 */
#ifndef SAFI_RENDER_MATERIAL_H
#define SAFI_RENDER_MATERIAL_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL_gpu.h>

#include "safi/render/renderer.h"

typedef struct SafiMaterial {
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUTexture          *base_color;
    SDL_GPUSampler          *sampler;
} SafiMaterial;

/* Build the default unlit-textured pipeline. Loads examples shaders from
 * disk; in a real app you'd package them into the binary. */
bool safi_material_create_unlit(SafiRenderer *r,
                                SafiMaterial *out,
                                const char   *hlsl_path);

void safi_material_destroy(SafiRenderer *r, SafiMaterial *m);

/* Set the base color texture + size in pixels (RGBA8). Replaces any
 * previously bound texture. */
bool safi_material_set_base_color_rgba8(SafiRenderer  *r,
                                        SafiMaterial  *m,
                                        const uint8_t *pixels,
                                        uint32_t       width,
                                        uint32_t       height);

#endif /* SAFI_RENDER_MATERIAL_H */
