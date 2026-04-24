#include "safi/render/primitive_system.h"
#include "safi/render/primitive_mesh.h"
#include "safi/render/assets.h"
#include "safi/render/mesh.h"
#include "safi/render/model.h"
#include "safi/render/material.h"
#include "safi/ecs/components.h"
#include "safi/core/log.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

#ifndef SAFI_ENGINE_SHADER_DIR
#error "SAFI_ENGINE_SHADER_DIR not defined"
#endif

/* ---- Hash --------------------------------------------------------------- */

static uint64_t s_hash_bytes(const void *data, size_t len, uint64_t seed) {
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
    h = s_hash_bytes(&p->texture.id, sizeof(p->texture.id), h);
    if (h == 0) h = 1;
    return h;
}

/* ---- Build a SafiModel from a primitive --------------------------------- *
 *
 * Constructs mesh + material + wraps them into a SafiModel, then registers
 * the model with the asset registry. Ownership of every GPU resource flows
 * into the SafiModel; the registry frees everything when the refcount
 * reaches zero. */

static SafiModelHandle s_build_and_register(SafiRenderer *r,
                                             SafiPrimitive *p) {
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
        return (SafiModelHandle){0};
    }

    SafiMesh mesh;
    if (!safi_mesh_create(r, &mesh, verts, (uint32_t)nv, inds, (uint32_t)ni)) {
        free(verts); free(inds);
        return (SafiModelHandle){0};
    }
    free(verts); free(inds);

    SafiMaterial material;
    if (!safi_material_create_lit(r, &material, SAFI_ENGINE_SHADER_DIR)) {
        safi_mesh_destroy(r, &mesh);
        return (SafiModelHandle){0};
    }

    /* Replace the material's default white base color with the primitive's
     * color/texture. When a texture handle is attached, the material borrows
     * the registry's GPU texture (no re-upload, no refcount churn, hot-
     * reload propagates automatically). Otherwise fall back to a 1×1 solid-
     * color texture owned by the material. */
    SDL_GPUTexture *registry_tex = safi_handle_valid(p->texture.id)
        ? safi_assets_resolve_texture(p->texture) : NULL;
    bool borrowed_tex = false;
    if (registry_tex) {
        safi_material_set_base_color_borrowed(r, &material, registry_tex);
        borrowed_tex = true;
    } else {
        uint8_t rgba[4] = {
            (uint8_t)(p->color[0] * 255.0f + 0.5f),
            (uint8_t)(p->color[1] * 255.0f + 0.5f),
            (uint8_t)(p->color[2] * 255.0f + 0.5f),
            (uint8_t)(p->color[3] * 255.0f + 0.5f),
        };
        safi_material_set_base_color_rgba8(r, &material, rgba, 1, 1);
    }

    /* Build a single-primitive SafiModel that owns all the GPU resources.
     * After construction, the mesh and material structs are consumed — their
     * GPU handles now live inside the model. We zero them so they won't be
     * freed a second time. */
    SafiModel model;
    memset(&model, 0, sizeof(model));
    model.vbo            = mesh.vbo;
    model.ibo            = mesh.ibo;
    model.vertex_count   = mesh.vertex_count;
    model.index_count    = mesh.index_count;
    model.pipeline       = material.pipeline;
    model.sampler        = material.sampler;
    model.white_fallback = NULL;
    model.aabb_min[0]    = mesh.aabb_min[0];
    model.aabb_min[1]    = mesh.aabb_min[1];
    model.aabb_min[2]    = mesh.aabb_min[2];
    model.aabb_max[0]    = mesh.aabb_max[0];
    model.aabb_max[1]    = mesh.aabb_max[1];
    model.aabb_max[2]    = mesh.aabb_max[2];

    model.primitive_count = 1;
    model.primitives      = (SafiModelPrimitive *)calloc(1, sizeof(SafiModelPrimitive));
    model.primitives[0].index_offset   = 0;
    model.primitives[0].index_count    = mesh.index_count;
    model.primitives[0].material_index = 0;

    model.material_count = 1;
    model.base_colors    = (SDL_GPUTexture **)calloc(1, sizeof(SDL_GPUTexture *));
    model.base_colors[0] = material.base_color;
    /* Preserve the ownership bit from the material: registry-borrowed
     * textures must not be released by the model destructor. The array is
     * only allocated when at least one slot is non-owned so the glTF path
     * (which always owns) pays zero cost. */
    if (borrowed_tex) {
        model.base_color_owned    = (bool *)calloc(1, sizeof(bool));
        model.base_color_owned[0] = false;
    }

    /* Ownership transferred into model — zero the temporaries. */
    memset(&mesh, 0, sizeof(mesh));
    memset(&material, 0, sizeof(material));

    return safi_assets_register_model(model);
}

/* ---- System callback ---------------------------------------------------- */

static void primitive_system(ecs_iter_t *it) {
    SafiApp      *app = (SafiApp *)it->ctx;
    SafiRenderer *r   = &app->renderer;
    ecs_world_t  *world = it->world;

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
            if (p->_model_handle.id && h == p->_hash) continue;

            if (p->_model_handle.id) {
                safi_assets_release_model(p->_model_handle);
                p->_model_handle = (SafiModelHandle){0};
            }

            SafiModelHandle mh = s_build_and_register(r, p);
            if (!mh.id) {
                p->_hash = 0;
                continue;
            }
            p->_model_handle = mh;
            p->_hash = h;

            ecs_entity_t e = qit.entities[i];
            /* ecs_set triggers the SafiMeshRenderer .copy hook which releases
             * any previous handle and acquires mh. After this call the model
             * is held by two refs: SafiPrimitive._model_handle owns the
             * register_model's initial +1, and SafiMeshRenderer.model owns
             * the hook's acquire. Both components release on destruction. */
            ecs_set(world, e, SafiMeshRenderer,
                    { .model = mh, .visible = true });
        }
    }
    ecs_query_fini(q);
}

/* ---- Hot-reload hook ---------------------------------------------------- *
 *
 * `primitive_system` caches the registry's SDL_GPUTexture* inside the
 * built model's base_colors[] at rebuild time. When safi_assets_reload_texture
 * swaps the underlying pointer, those caches become stale. On every reload
 * event, scan all primitives and invalidate `_hash` for anyone referencing
 * the reloaded texture id — the next primitive_system tick will rebuild
 * them and re-resolve through safi_assets_resolve_texture. Model reloads
 * don't need this since the render system resolves the model handle each
 * frame. */

static void on_asset_reload(uint32_t handle_id, void *ctx) {
    ecs_world_t *world = (ecs_world_t *)ctx;
    if (!world) return;

    ecs_query_t *q = ecs_query(world, {
        .terms      = {{ .id = ecs_id(SafiPrimitive) }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        SafiPrimitive *prims = ecs_field(&it, SafiPrimitive, 0);
        for (int i = 0; i < it.count; i++) {
            if (prims[i].texture.id == handle_id) {
                prims[i]._hash = 0;
            }
        }
    }
    ecs_query_fini(q);
}

/* ---- Init --------------------------------------------------------------- *
 *
 * Handle release on component removal / entity destruction is handled by the
 * SafiPrimitive .dtor hook registered in component_serializers.c, so no
 * EcsOnRemove observer is needed here. */

void safi_primitive_system_init(ecs_world_t *world, SafiApp *app) {
    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "safi_primitive_system",
            .add  = ecs_ids(ecs_dependson(EcsPreStore)),
        }),
        .callback = primitive_system,
        .ctx      = app,
    });

    /* Subscribe to texture hot-reloads so rebuilt models pick up the
     * swapped SDL_GPUTexture* instead of rendering a freed pointer. */
    safi_assets_on_reload(on_asset_reload, world);
}
