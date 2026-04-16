#include "safi/render/primitive_system.h"
#include "safi/render/primitive_mesh.h"
#include "safi/render/mesh.h"
#include "safi/render/model.h"
#include "safi/render/material.h"
#include "safi/ecs/components.h"
#include "safi/core/log.h"

#include <SDL3/SDL.h>
#include <stb_image.h>

#include <stdlib.h>
#include <string.h>

#ifndef SAFI_ENGINE_SHADER_DIR
#error "SAFI_ENGINE_SHADER_DIR not defined"
#endif

/* Private GPU-side bundle for a single primitive. We build a minimal
 * SafiModel (one primitive range, one material) so the standard render
 * system reaches it through SafiMeshRenderer.model without any special
 * case. */
typedef struct SafiPrimitiveGpu {
    SafiMesh     mesh;      /* owns vbo + ibo */
    SafiMaterial material;  /* owns pipeline + sampler + base_color texture */
    SafiModel    model;     /* views `mesh` buffers + references `material` */
    /* Cached pointer to the texture slot inside `model.base_colors[0]` so we
     * can re-point it after a rebuild without reallocating the array. */
    SDL_GPUTexture **model_tex_slot;
    uint32_t         model_prims_cap;
} SafiPrimitiveGpu;

/* ---- Hash --------------------------------------------------------------- */

static uint64_t s_hash_bytes(const void *data, size_t len, uint64_t seed) {
    /* FNV-1a 64-bit. Cheap and good enough for change detection. */
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = seed ? seed : 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static uint64_t s_hash_primitive(const SafiPrimitive *p) {
    uint64_t h = s_hash_bytes(&p->shape, sizeof(p->shape), 0);
    h = s_hash_bytes(&p->dims, sizeof(p->dims), h);
    h = s_hash_bytes(p->color, sizeof(p->color), h);
    h = s_hash_bytes(p->texture_path, strlen(p->texture_path), h);
    if (h == 0) h = 1;  /* reserve 0 for "unbuilt" */
    return h;
}

/* ---- Texture upload ----------------------------------------------------- */

static SDL_GPUTexture *s_upload_rgba8(SafiRenderer  *r,
                                      const uint8_t *pixels,
                                      uint32_t       w,
                                      uint32_t       h) {
    SDL_GPUTextureCreateInfo ti = {
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .width                = w,
        .height               = h,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
        .sample_count         = SDL_GPU_SAMPLECOUNT_1,
        .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER,
    };
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(r->device, &ti);
    if (!tex) return NULL;

    uint32_t byte_size = w * h * 4u;
    SDL_GPUTransferBufferCreateInfo tbi = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = byte_size,
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(r->device, &tbi);
    void *mapped = SDL_MapGPUTransferBuffer(r->device, tb, false);
    memcpy(mapped, pixels, byte_size);
    SDL_UnmapGPUTransferBuffer(r->device, tb);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(r->device);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = {
        .transfer_buffer = tb,
        .pixels_per_row  = w,
        .rows_per_layer  = h,
    };
    SDL_GPUTextureRegion dst = { .texture = tex, .w = w, .h = h, .d = 1 };
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(r->device, tb);
    return tex;
}

/* Build a GPU texture from the primitive's color/texture_path. Always
 * returns a valid texture so the fragment shader sampler has something to
 * bind — falls back to a 1×1 solid color whenever file decode fails. */
static SDL_GPUTexture *s_build_texture(SafiRenderer *r, const SafiPrimitive *p) {
    if (p->texture_path[0] != '\0') {
        int w = 0, h = 0, n = 0;
        uint8_t *pixels = stbi_load(p->texture_path, &w, &h, &n, 4);
        if (pixels) {
            SDL_GPUTexture *tex = s_upload_rgba8(r, pixels,
                                                 (uint32_t)w, (uint32_t)h);
            stbi_image_free(pixels);
            if (tex) {
                SAFI_LOG_INFO("primitive: loaded texture '%s' (%dx%d)",
                              p->texture_path, w, h);
                return tex;
            }
        } else {
            SAFI_LOG_WARN("primitive: failed to load '%s' (%s), "
                          "falling back to solid color",
                          p->texture_path, stbi_failure_reason());
        }
    }

    uint8_t rgba[4] = {
        (uint8_t)(p->color[0] * 255.0f + 0.5f),
        (uint8_t)(p->color[1] * 255.0f + 0.5f),
        (uint8_t)(p->color[2] * 255.0f + 0.5f),
        (uint8_t)(p->color[3] * 255.0f + 0.5f),
    };
    return s_upload_rgba8(r, rgba, 1, 1);
}

/* ---- Build / rebuild ---------------------------------------------------- */

static void s_free_gpu(SafiRenderer *r, SafiPrimitiveGpu *g) {
    if (!g) return;
    /* The model shares vbo/ibo with the mesh and the texture with the
     * material — release each owner exactly once. */
    safi_material_destroy(r, &g->material);
    safi_mesh_destroy(r, &g->mesh);
    free(g->model.primitives);
    free(g->model.base_colors);
    memset(&g->model, 0, sizeof(g->model));
    free(g);
}

static bool s_build_gpu(SafiRenderer *r, SafiPrimitive *p,
                        SafiPrimitiveGpu *g) {
    SafiVertex *verts = NULL;
    uint32_t   *inds  = NULL;
    size_t      nv = 0, ni = 0;
    bool ok = false;

    switch (p->shape) {
    case SAFI_PRIMITIVE_PLANE:
        ok = safi_primitive_build_plane(p->dims.plane.size,
                                        &verts, &nv, &inds, &ni);
        break;
    case SAFI_PRIMITIVE_BOX:
        ok = safi_primitive_build_box(p->dims.box.half_extents,
                                      &verts, &nv, &inds, &ni);
        break;
    case SAFI_PRIMITIVE_SPHERE:
        ok = safi_primitive_build_sphere(p->dims.sphere.radius,
                                         p->dims.sphere.segments,
                                         p->dims.sphere.rings,
                                         &verts, &nv, &inds, &ni);
        break;
    case SAFI_PRIMITIVE_CAPSULE:
        ok = safi_primitive_build_capsule(p->dims.capsule.radius,
                                          p->dims.capsule.height,
                                          p->dims.capsule.segments,
                                          p->dims.capsule.rings,
                                          &verts, &nv, &inds, &ni);
        break;
    }
    if (!ok) {
        SAFI_LOG_WARN("primitive: geometry build failed (shape=%d)", p->shape);
        return false;
    }

    if (!safi_mesh_create(r, &g->mesh, verts, (uint32_t)nv, inds, (uint32_t)ni)) {
        free(verts); free(inds);
        return false;
    }
    free(verts); free(inds);

    if (!safi_material_create_lit(r, &g->material, SAFI_ENGINE_SHADER_DIR)) {
        safi_mesh_destroy(r, &g->mesh);
        return false;
    }

    /* Replace the material's default 1×1 white base color with ours. */
    if (g->material.base_color) {
        SDL_ReleaseGPUTexture(r->device, g->material.base_color);
        g->material.base_color = NULL;
    }
    g->material.base_color = s_build_texture(r, p);
    if (!g->material.base_color) {
        safi_material_destroy(r, &g->material);
        safi_mesh_destroy(r, &g->mesh);
        return false;
    }

    /* Wrap the mesh in a minimal SafiModel so the existing render path
     * (safi_model_draw_lit) works unchanged. Ownership note: this SafiModel
     * views buffers owned by `mesh` and `material`. We must NEVER call
     * safi_model_destroy() on it — we free its per-view allocations (the
     * primitives array and base_colors array) manually in s_free_gpu. */
    memset(&g->model, 0, sizeof(g->model));
    g->model.vbo              = g->mesh.vbo;
    g->model.ibo              = g->mesh.ibo;
    g->model.vertex_count     = g->mesh.vertex_count;
    g->model.index_count      = g->mesh.index_count;
    g->model.pipeline         = g->material.pipeline;
    g->model.sampler          = g->material.sampler;
    g->model.white_fallback   = NULL;    /* not used; tex is always valid */
    g->model.aabb_min[0]      = g->mesh.aabb_min[0];
    g->model.aabb_min[1]      = g->mesh.aabb_min[1];
    g->model.aabb_min[2]      = g->mesh.aabb_min[2];
    g->model.aabb_max[0]      = g->mesh.aabb_max[0];
    g->model.aabb_max[1]      = g->mesh.aabb_max[1];
    g->model.aabb_max[2]      = g->mesh.aabb_max[2];

    g->model.primitive_count  = 1;
    g->model.primitives       = (SafiModelPrimitive *)calloc(1, sizeof(SafiModelPrimitive));
    g->model.primitives[0].index_offset   = 0;
    g->model.primitives[0].index_count    = g->mesh.index_count;
    g->model.primitives[0].material_index = 0;

    g->model.material_count   = 1;
    g->model.base_colors      = (SDL_GPUTexture **)calloc(1, sizeof(SDL_GPUTexture *));
    g->model.base_colors[0]   = g->material.base_color;

    return true;
}

/* ---- System callback ---------------------------------------------------- */

static void primitive_system(ecs_iter_t *it) {
    SafiApp      *app = (SafiApp *)it->ctx;
    SafiRenderer *r   = &app->renderer;
    ecs_world_t  *world = it->world;

    /* flecs guarantees structural mutations here are deferred until after
     * the iteration, so adding SafiMeshRenderer from inside this loop is
     * safe. */
    ecs_query_t *q = ecs_query(world, {
        .terms = {{ .id = ecs_id(SafiPrimitive) }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_iter_t qit = ecs_query_iter(world, q);
    while (ecs_query_next(&qit)) {
        SafiPrimitive *prims = ecs_field(&qit, SafiPrimitive, 0);
        for (int i = 0; i < qit.count; i++) {
            SafiPrimitive *p = &prims[i];
            uint64_t h = s_hash_primitive(p);
            if (p->_gpu && h == p->_hash) continue;

            /* Rebuild. */
            if (p->_gpu) {
                s_free_gpu(r, p->_gpu);
                p->_gpu = NULL;
            }

            SafiPrimitiveGpu *g = (SafiPrimitiveGpu *)calloc(1, sizeof(*g));
            if (!g) continue;
            if (!s_build_gpu(r, p, g)) {
                free(g);
                p->_hash = 0;
                continue;
            }
            p->_gpu  = g;
            p->_hash = h;

            /* Auto-attach / refresh the MeshRenderer so the standard render
             * system sees this entity. Overwriting .model each rebuild is
             * necessary because the SafiModel sits inside a heap-allocated
             * SafiPrimitiveGpu whose address may change across rebuilds. */
            ecs_entity_t e = qit.entities[i];
            SafiMeshRenderer mr = { .model = &g->model, .visible = true };
            if (!ecs_has(world, e, SafiMeshRenderer)) {
                ecs_set_ptr(world, e, SafiMeshRenderer, &mr);
            } else {
                SafiMeshRenderer *existing = ecs_get_mut(world, e, SafiMeshRenderer);
                existing->model   = &g->model;
                existing->visible = true;
            }
        }
    }
    ecs_query_fini(q);
}

/* ---- on_remove observer ------------------------------------------------- */

static void primitive_on_remove(ecs_iter_t *it) {
    SafiApp      *app = (SafiApp *)it->ctx;
    SafiRenderer *r   = &app->renderer;
    SafiPrimitive *prims = ecs_field(it, SafiPrimitive, 0);
    for (int i = 0; i < it->count; i++) {
        if (prims[i]._gpu) {
            s_free_gpu(r, prims[i]._gpu);
            prims[i]._gpu  = NULL;
            prims[i]._hash = 0;
        }
    }
}

/* ---- Init --------------------------------------------------------------- */

void safi_primitive_system_init(ecs_world_t *world, SafiApp *app) {
    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "safi_primitive_system",
            .add  = ecs_ids(ecs_dependson(EcsPreStore)),
        }),
        .callback = primitive_system,
        .ctx      = app,
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(SafiPrimitive) }},
        .events      = { EcsOnRemove },
        .callback    = primitive_on_remove,
        .ctx         = app,
    });
}
