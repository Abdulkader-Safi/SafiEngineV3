#include "systems/render_system.h"
#include "demo_state.h"

#include <safi/safi.h>
#include <safi/ui/debug_ui.h>

#include <string.h>

void render_system(ecs_iter_t *it) {
  SafiApp *a = (SafiApp *)it->ctx;

  SafiRenderer *r = &a->renderer;
  if (!safi_renderer_begin_frame(r))
    return;

  SafiCamera *cam = ecs_get_mut(it->world, g_demo.camera_entity, SafiCamera);
  SafiGlobalTransform *gt =
      ecs_get_mut(it->world, g_demo.model_entity, SafiGlobalTransform);
  if (!cam || !gt) {
    safi_renderer_end_frame(r);
    return;
  }

  /* ---- Build MicroUI widgets (pre-pass) ------------------------------- */
  if (a->debug_ui_enabled) {
    safi_debug_ui_begin_frame(r);
    safi_debug_ui_draw_panels(r, it->world);
    safi_debug_ui_prepare(r);
  }

  /* ---- Build matrices ------------------------------------------------- */
  mat4 view, proj, model_mat, mvp;
  vec3 eye = {cam->target[0], cam->target[1], cam->target[2] + 3.0f};
  vec3 center = {0, 0, 0};
  vec3 up = {0, 1, 0};
  glm_lookat(eye, center, up, view);

  float aspect = (float)r->swapchain_w / (float)r->swapchain_h;
  glm_perspective(cam->fov_y_radians, aspect, cam->z_near, cam->z_far, proj);

  /* World-space model matrix comes from the engine's transform
   * propagation system (EcsPostUpdate) — no inline TRS math here. */
  glm_mat4_copy(gt->matrix, model_mat);

  glm_mat4_mul(proj, view, mvp);
  glm_mat4_mul(mvp, model_mat, mvp);

  /* ---- Collect lights from ECS --------------------------------------- */
  SafiLightBuffer light_buf;
  safi_light_buffer_collect(it->world, &light_buf);

  /* ---- Build GPU uniform structs ------------------------------------ */
  SafiLitVSUniforms vs_buf;
  memcpy(vs_buf.model, model_mat, sizeof(model_mat));
  memcpy(vs_buf.mvp, mvp, sizeof(mvp));
  safi_compute_normal_matrix((const float *)model_mat, vs_buf.normal_mat);

  SafiCameraBuffer cam_buf;
  memcpy(cam_buf.view, view, sizeof(view));
  memcpy(cam_buf.proj, proj, sizeof(proj));
  cam_buf.eye_pos[0] = eye[0];
  cam_buf.eye_pos[1] = eye[1];
  cam_buf.eye_pos[2] = eye[2];
  cam_buf._pad = 0.0f;

  /* ---- Main render pass --------------------------------------------- */
  safi_renderer_begin_main_pass(r);

  SDL_GPUViewport vp = {
      .x = 0.0f,
      .y = 0.0f,
      .w = (float)r->swapchain_w,
      .h = (float)r->swapchain_h,
      .min_depth = 0.0f,
      .max_depth = 1.0f,
  };
  SDL_SetGPUViewport(r->pass, &vp);
  SDL_Rect full = {0, 0, (int)r->swapchain_w, (int)r->swapchain_h};
  SDL_SetGPUScissor(r->pass, &full);

  safi_model_draw_lit(r, &g_demo.model, &vs_buf, &cam_buf, &light_buf);

  if (a->debug_ui_enabled)
    safi_debug_ui_render(r);

  safi_renderer_end_main_pass(r);
  safi_renderer_end_frame(r);
}
