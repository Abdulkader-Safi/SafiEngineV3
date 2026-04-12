#include "safi/render/render_system.h"
#include "safi/core/app.h"
#include "safi/ecs/components.h"
#include "safi/render/renderer.h"
#include "safi/render/model.h"
#include "safi/render/light_buffer.h"
#include "safi/render/light_system.h"
#include "safi/ui/debug_ui.h"

#include <cglm/cglm.h>
#include <string.h>

/* Engine-owned render system.
 *
 * Frame shape (SDL_gpu forbids nested passes, so the UI's vertex upload
 * must happen BEFORE the main render pass opens):
 *
 *   begin_frame                    -> cmd + swapchain
 *   debug_ui_begin_frame           -> mu_begin
 *   <build widgets>
 *   debug_ui_prepare               -> mu_end + batch quads + copy-pass upload
 *   begin_main_pass                -> open color+depth pass
 *   <draw each renderable entity>
 *   debug_ui_render                -> record batched draws into the pass
 *   end_main_pass
 *   end_frame                      -> submit
 */
static void safi_render_system(ecs_iter_t *it) {
    SafiApp *a = (SafiApp *)it->ctx;
    SafiRenderer *r = &a->renderer;
    if (!safi_renderer_begin_frame(r)) return;

    /* ---- Find the active camera ----------------------------------------- */
    SafiCamera *cam = NULL;
    {
        ecs_query_t *q = ecs_query(it->world, {
            .terms = {
                { .id = ecs_id(SafiCamera) },
                { .id = ecs_id(SafiActiveCamera) },
            },
            .cache_kind = EcsQueryCacheNone,
        });
        ecs_iter_t qit = ecs_query_iter(it->world, q);
        while (ecs_query_next(&qit)) {
            if (!cam) cam = ecs_field(&qit, SafiCamera, 0);
        }
        ecs_query_fini(q);
    }
    if (!cam) { safi_renderer_end_frame(r); return; }

    /* ---- Build MicroUI widgets (pre-pass) ------------------------------- */
    if (a->debug_ui_enabled) {
        safi_debug_ui_begin_frame(r);
        safi_debug_ui_draw_panels(r, it->world);
        safi_debug_ui_prepare(r);
    }

    /* ---- Build view / projection ---------------------------------------- */
    mat4 view, proj;
    vec3 eye = { cam->target[0], cam->target[1], cam->target[2] + 3.0f };
    vec3 center = { 0, 0, 0 };
    vec3 up = { 0, 1, 0 };
    glm_lookat(eye, center, up, view);

    float aspect = (float)r->swapchain_w / (float)r->swapchain_h;
    glm_perspective(cam->fov_y_radians, aspect, cam->z_near, cam->z_far, proj);

    /* ---- Collect lights from ECS ---------------------------------------- */
    SafiLightBuffer light_buf;
    safi_light_buffer_collect(it->world, &light_buf);

    /* ---- Camera uniform buffer (shared across all draws) ---------------- */
    SafiCameraBuffer cam_buf;
    memcpy(cam_buf.view, view, sizeof(view));
    memcpy(cam_buf.proj, proj, sizeof(proj));
    cam_buf.eye_pos[0] = eye[0];
    cam_buf.eye_pos[1] = eye[1];
    cam_buf.eye_pos[2] = eye[2];
    cam_buf._pad = 0.0f;

    /* ---- Main render pass ----------------------------------------------- */
    safi_renderer_begin_main_pass(r);

    SDL_GPUViewport vp = {
        .x = 0.0f, .y = 0.0f,
        .w = (float)r->swapchain_w, .h = (float)r->swapchain_h,
        .min_depth = 0.0f, .max_depth = 1.0f,
    };
    SDL_SetGPUViewport(r->pass, &vp);
    SDL_Rect full = { 0, 0, (int)r->swapchain_w, (int)r->swapchain_h };
    SDL_SetGPUScissor(r->pass, &full);

    /* ---- Draw each renderable entity ------------------------------------ */
    {
        ecs_query_t *q = ecs_query(it->world, {
            .terms = {
                { .id = ecs_id(SafiGlobalTransform) },
                { .id = ecs_id(SafiMeshRenderer) },
            },
            .cache_kind = EcsQueryCacheNone,
        });
        ecs_iter_t qit = ecs_query_iter(it->world, q);
        while (ecs_query_next(&qit)) {
            SafiGlobalTransform *gt = ecs_field(&qit, SafiGlobalTransform, 0);
            SafiMeshRenderer    *mr = ecs_field(&qit, SafiMeshRenderer, 1);
            for (int i = 0; i < qit.count; i++) {
                if (!mr[i].model || !mr[i].visible) continue;

                mat4 model_mat, mvp;
                glm_mat4_copy(gt[i].matrix, model_mat);
                glm_mat4_mul(proj, view, mvp);
                glm_mat4_mul(mvp, model_mat, mvp);

                SafiLitVSUniforms vs_buf;
                memcpy(vs_buf.model, model_mat, sizeof(model_mat));
                memcpy(vs_buf.mvp, mvp, sizeof(mvp));
                safi_compute_normal_matrix((const float *)model_mat, vs_buf.normal_mat);

                safi_model_draw_lit(r, mr[i].model, &vs_buf, &cam_buf, &light_buf);
            }
        }
        ecs_query_fini(q);
    }

    if (a->debug_ui_enabled) safi_debug_ui_render(r);

    safi_renderer_end_main_pass(r);
    safi_renderer_end_frame(r);
}

void safi_render_system_init(ecs_world_t *world, SafiApp *app) {
    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "safi_render_system",
            .add  = ecs_ids(ecs_dependson(EcsOnStore)),
        }),
        .callback = safi_render_system,
        .ctx      = app,
    });
}
