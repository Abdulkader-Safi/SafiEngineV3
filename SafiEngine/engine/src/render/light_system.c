#include "safi/render/light_system.h"
#include "safi/ecs/components.h"
#include "safi/core/log.h"

#include <string.h>
#include <cglm/cglm.h>

/* Derive forward direction (-Z) from a quaternion. */
static void s_forward_from_quat(const versor q, float out[3]) {
    vec3 fwd = { 0.0f, 0.0f, -1.0f };
    glm_quat_rotatev((float *)q, fwd, out);
}

void safi_light_buffer_collect(ecs_world_t *world, SafiLightBuffer *out) {
    memset(out, 0, sizeof(*out));
    uint32_t idx = 0;

    /* ---- Directional lights -------------------------------------------- */
    {
        ecs_query_desc_t desc = {
            .terms = {{ .id = ecs_id(SafiDirectionalLight) }}
        };
        ecs_query_t *q = ecs_query_init(world, &desc);
        ecs_iter_t it = ecs_query_iter(world, q);
        while (ecs_query_next(&it)) {
            const SafiDirectionalLight *dl = ecs_field(&it, SafiDirectionalLight, 0);
            for (int i = 0; i < it.count && idx < SAFI_MAX_LIGHTS; i++, idx++) {
                SafiGPULight *g = &out->lights[idx];
                g->type = SAFI_LIGHT_TYPE_DIRECTIONAL;
                g->direction[0] = dl[i].direction[0];
                g->direction[1] = dl[i].direction[1];
                g->direction[2] = dl[i].direction[2];
                g->color[0] = dl[i].color[0];
                g->color[1] = dl[i].color[1];
                g->color[2] = dl[i].color[2];
                g->intensity = dl[i].intensity;
            }
        }
        ecs_query_fini(q);
    }

    /* ---- Point lights -------------------------------------------------- */
    {
        ecs_query_desc_t desc = {
            .terms = {
                { .id = ecs_id(SafiPointLight) },
                { .id = ecs_id(SafiTransform) }
            }
        };
        ecs_query_t *q = ecs_query_init(world, &desc);
        ecs_iter_t it = ecs_query_iter(world, q);
        while (ecs_query_next(&it)) {
            const SafiPointLight *pl = ecs_field(&it, SafiPointLight, 0);
            const SafiTransform  *xf = ecs_field(&it, SafiTransform, 1);
            for (int i = 0; i < it.count && idx < SAFI_MAX_LIGHTS; i++, idx++) {
                SafiGPULight *g = &out->lights[idx];
                g->type = SAFI_LIGHT_TYPE_POINT;
                g->position[0] = xf[i].position[0];
                g->position[1] = xf[i].position[1];
                g->position[2] = xf[i].position[2];
                g->color[0] = pl[i].color[0];
                g->color[1] = pl[i].color[1];
                g->color[2] = pl[i].color[2];
                g->intensity = pl[i].intensity;
                g->range = pl[i].range;
            }
        }
        ecs_query_fini(q);
    }

    /* ---- Spot lights --------------------------------------------------- */
    {
        ecs_query_desc_t desc = {
            .terms = {
                { .id = ecs_id(SafiSpotLight) },
                { .id = ecs_id(SafiTransform) }
            }
        };
        ecs_query_t *q = ecs_query_init(world, &desc);
        ecs_iter_t it = ecs_query_iter(world, q);
        while (ecs_query_next(&it)) {
            const SafiSpotLight *sl = ecs_field(&it, SafiSpotLight, 0);
            const SafiTransform *xf = ecs_field(&it, SafiTransform, 1);
            for (int i = 0; i < it.count && idx < SAFI_MAX_LIGHTS; i++, idx++) {
                SafiGPULight *g = &out->lights[idx];
                g->type = SAFI_LIGHT_TYPE_SPOT;
                g->position[0] = xf[i].position[0];
                g->position[1] = xf[i].position[1];
                g->position[2] = xf[i].position[2];
                float fwd[3];
                s_forward_from_quat(xf[i].rotation, fwd);
                g->direction[0] = fwd[0];
                g->direction[1] = fwd[1];
                g->direction[2] = fwd[2];
                g->color[0] = sl[i].color[0];
                g->color[1] = sl[i].color[1];
                g->color[2] = sl[i].color[2];
                g->intensity = sl[i].intensity;
                g->range = sl[i].range;
                g->inner_angle = sl[i].inner_angle;
                g->outer_angle = sl[i].outer_angle;
            }
        }
        ecs_query_fini(q);
    }

    /* ---- Rect lights --------------------------------------------------- */
    {
        ecs_query_desc_t desc = {
            .terms = {
                { .id = ecs_id(SafiRectLight) },
                { .id = ecs_id(SafiTransform) }
            }
        };
        ecs_query_t *q = ecs_query_init(world, &desc);
        ecs_iter_t it = ecs_query_iter(world, q);
        while (ecs_query_next(&it)) {
            const SafiRectLight *rl = ecs_field(&it, SafiRectLight, 0);
            const SafiTransform *xf = ecs_field(&it, SafiTransform, 1);
            for (int i = 0; i < it.count && idx < SAFI_MAX_LIGHTS; i++, idx++) {
                SafiGPULight *g = &out->lights[idx];
                g->type = SAFI_LIGHT_TYPE_RECT;
                g->position[0] = xf[i].position[0];
                g->position[1] = xf[i].position[1];
                g->position[2] = xf[i].position[2];
                float fwd[3];
                s_forward_from_quat(xf[i].rotation, fwd);
                g->direction[0] = fwd[0];
                g->direction[1] = fwd[1];
                g->direction[2] = fwd[2];
                g->color[0] = rl[i].color[0];
                g->color[1] = rl[i].color[1];
                g->color[2] = rl[i].color[2];
                g->intensity = rl[i].intensity;
                g->range = 0.0f;
                g->width = rl[i].width;
                g->height = rl[i].height;
            }
        }
        ecs_query_fini(q);
    }

    /* ---- Sky lights (ambient, not in the light array) ------------------- */
    {
        ecs_query_desc_t desc = {
            .terms = {{ .id = ecs_id(SafiSkyLight) }}
        };
        ecs_query_t *q = ecs_query_init(world, &desc);
        ecs_iter_t it = ecs_query_iter(world, q);
        while (ecs_query_next(&it)) {
            const SafiSkyLight *sk = ecs_field(&it, SafiSkyLight, 0);
            if (it.count > 0) {
                out->ambient_color[0] = sk[0].color[0];
                out->ambient_color[1] = sk[0].color[1];
                out->ambient_color[2] = sk[0].color[2];
                out->ambient_intensity = sk[0].intensity;
            }
        }
        ecs_query_fini(q);
    }

    if (idx > SAFI_MAX_LIGHTS) {
        SAFI_LOG_WARN("light_system: %u lights exceed max %d, clamped",
                      idx, SAFI_MAX_LIGHTS);
        idx = SAFI_MAX_LIGHTS;
    }
    out->light_count = idx;
}
