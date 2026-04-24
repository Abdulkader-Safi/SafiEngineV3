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
    /* When true, `safi_material_destroy` releases `base_color`. When false,
     * ownership lives elsewhere (e.g. the asset registry) and the material
     * only borrows the pointer. Defaults to true for backwards-compat — any
     * caller that wants to share a registry texture flips it to false via
     * `safi_material_set_base_color_borrowed`. */
    bool                     owns_base_color;
} SafiMaterial;

/* Build the default unlit-textured pipeline. `shader_dir` points at the
 * directory containing the compiled "unlit.<vert|frag>.<spv|msl>" artifacts
 * produced by the CMake shader build (see cmake/SafiShaders.cmake). The
 * loader picks the right format based on the active SDL_GPU backend. */
bool safi_material_create_unlit(SafiRenderer *r,
                                SafiMaterial *out,
                                const char   *shader_dir);

/* Build the Blinn-Phong lit pipeline. Same vertex layout as unlit but the
 * shaders receive additional uniform buffers for camera and light data. */
bool safi_material_create_lit(SafiRenderer *r,
                              SafiMaterial *out,
                              const char   *shader_dir);

void safi_material_destroy(SafiRenderer *r, SafiMaterial *m);

/* Set the base color texture + size in pixels (RGBA8). Replaces any
 * previously bound texture. Material takes ownership of the new GPU
 * texture and will release it on destroy. */
bool safi_material_set_base_color_rgba8(SafiRenderer  *r,
                                        SafiMaterial  *m,
                                        const uint8_t *pixels,
                                        uint32_t       width,
                                        uint32_t       height);

/* Point the material at an externally-owned GPU texture (typically one
 * resolved from the asset registry). Releases any previously-owned texture
 * before the swap so repeated rebuilds don't leak. Ownership stays with the
 * caller; the material just borrows the pointer. */
void safi_material_set_base_color_borrowed(SafiRenderer   *r,
                                            SafiMaterial   *m,
                                            SDL_GPUTexture *tex);

#endif /* SAFI_RENDER_MATERIAL_H */
