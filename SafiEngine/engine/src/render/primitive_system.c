#include "safi/render/primitive_system.h"
#include "safi/render/primitive_mesh.h"
#include "safi/render/assets.h"
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
    h = s_hash_bytes(p->texture_path, strlen(p->texture_path), h);
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
     * color/texture. Textures are NOT loaded through the asset registry here
     * because the SafiModel will own the texture pointer — if the registry
     * also held a reference, safi_model_destroy would double-free it. */
    if (p->texture_path[0] != '\0') {
        int tw = 0, th = 0, tn = 0;
        uint8_t *pixels = stbi_load(p->texture_path, &tw, &th, &tn, 4);
        if (pixels) {
            safi_material_set_base_color_rgba8(r, &material, pixels,
                                               (uint32_t)tw, (uint32_t)th);
            stbi_image_free(pixels);
            SAFI_LOG_INFO("primitive: loaded texture '%s' (%dx%d)",
                          p->texture_path, tw, th);
        } else {
            SAFI_LOG_WARN("primitive: failed to load '%s' (%s), "
                          "falling back to solid color",
                          p->texture_path, stbi_failure_reason());
        }
    }

    if (!p->texture_path[0]) {
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
            if (!ecs_has(world, e, SafiMeshRenderer)) {
                ecs_set(world, e, SafiMeshRenderer,
                        { .model = mh, .visible = true });
            } else {
                SafiMeshRenderer *existing =
                    ecs_get_mut(world, e, SafiMeshRenderer);
                existing->model   = mh;
                existing->visible = true;
            }
        }
    }
    ecs_query_fini(q);
}

/* ---- on_remove observer ------------------------------------------------- */

static void primitive_on_remove(ecs_iter_t *it) {
    SafiPrimitive *prims = ecs_field(it, SafiPrimitive, 0);
    for (int i = 0; i < it->count; i++) {
        if (prims[i]._model_handle.id) {
            safi_assets_release_model(prims[i]._model_handle);
            prims[i]._model_handle = (SafiModelHandle){0};
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
