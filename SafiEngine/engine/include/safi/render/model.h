/**
 * safi/render/model.h — high-level loaded glTF model.
 *
 * A SafiModel owns everything needed to render a glTF file: a shared
 * VBO/IBO containing all primitives, per-primitive draw ranges, and
 * per-material base-color textures. safi_model_draw() loops over
 * primitives and issues one draw call per material switch.
 *
 * For single-primitive models this behaves identically to the low-level
 * SafiMesh + SafiMaterial approach. For models with multiple materials
 * (common in character models, environments, etc.) each primitive
 * renders with its own texture.
 */
#ifndef SAFI_RENDER_MODEL_H
#define SAFI_RENDER_MODEL_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL_gpu.h>

#include "safi/render/renderer.h"
#include "safi/render/light_buffer.h"

typedef struct SafiModelPrimitive {
    uint32_t index_offset;     /* first index (element offset, not bytes) */
    uint32_t index_count;      /* number of indices */
    uint32_t material_index;   /* into SafiModel.base_colors[] */
} SafiModelPrimitive;

typedef struct SafiModel {
    /* Shared GPU geometry */
    SDL_GPUBuffer *vbo;
    SDL_GPUBuffer *ibo;
    uint32_t       vertex_count;
    uint32_t       index_count;

    /* Per-primitive draw ranges */
    SafiModelPrimitive *primitives;
    uint32_t            primitive_count;

    /* Per-material textures (pipeline + sampler are shared) */
    SDL_GPUTexture         **base_colors;    /* [material_count], NULL = use fallback */
    uint32_t                 material_count;
    SDL_GPUGraphicsPipeline *pipeline;       /* shared unlit pipeline */
    SDL_GPUSampler          *sampler;        /* shared sampler */
    SDL_GPUTexture          *white_fallback; /* 1x1 white for materials w/o texture */

    float aabb_min[3];
    float aabb_max[3];
} SafiModel;

/* Load a glTF file into a SafiModel. Iterates all meshes × primitives,
 * merges geometry into shared buffers, and loads one base-color texture
 * per unique material. shader_dir is the directory containing compiled
 * unlit shader artifacts (see cmake/SafiShaders.cmake). */
bool safi_model_load(SafiRenderer *r,
                     const char   *path,
                     const char   *shader_dir,
                     SafiModel    *out);

/* Draw all primitives with their materials. Must be called inside an
 * active render pass (between begin_main_pass / end_main_pass). */
void safi_model_draw(SafiRenderer    *r,
                     const SafiModel *model,
                     const float     *mvp);  /* mat4, 16 floats */

/* Load a glTF file with the Blinn-Phong lit pipeline. Same as
 * safi_model_load() but creates a lit graphics pipeline instead of unlit. */
bool safi_model_load_lit(SafiRenderer *r,
                         const char   *path,
                         const char   *shader_dir,
                         SafiModel    *out);

/* Draw all primitives with lit shading. Must be called inside an active
 * render pass. Pushes camera + light uniform data to the fragment shader. */
void safi_model_draw_lit(SafiRenderer            *r,
                         const SafiModel          *model,
                         const SafiLitVSUniforms  *vs_uniforms,
                         const SafiCameraBuffer   *camera,
                         const SafiLightBuffer    *lights);

void safi_model_destroy(SafiRenderer *r, SafiModel *model);

#endif /* SAFI_RENDER_MODEL_H */
