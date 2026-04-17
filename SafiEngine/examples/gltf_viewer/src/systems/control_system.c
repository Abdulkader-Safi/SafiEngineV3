#include "systems/control_system.h"
#include "demo_state.h"

#include <safi/safi.h>
#include <safi/ui/debug_ui.h>

#include <SDL3/SDL.h>
#include <microui.h>

void control_system(ecs_iter_t *it) {
  /* Skip game keyboard controls when a MicroUI widget is active (e.g. the
   * user is typing a number into a property field). */
  if (safi_debug_ui_wants_input())
    return;

  const SafiInput *in = ecs_singleton_get(it->world, SafiInput);
  if (!in)
    return;

  float dt = it->delta_time;
  float rate = 1.5f; /* rad/s */

  /* Rotate the model entity (arrows + A/D) regardless of selection. */
  SafiTransform *xform =
      ecs_get_mut(it->world, g_demo.model_entity, SafiTransform);
  if (xform) {
#define APPLY_DELTA(angle, ax, ay, az)                                         \
  do {                                                                         \
    versor _q, _tmp;                                                           \
    glm_quatv(_q, (angle), (vec3){(ax), (ay), (az)});                          \
    glm_quat_mul(_q, xform->rotation, _tmp);                                   \
    glm_quat_copy(_tmp, xform->rotation);                                      \
  } while (0)

    if (in->keys[SDL_SCANCODE_LEFT])
      APPLY_DELTA(rate * dt, 0, 1, 0);
    if (in->keys[SDL_SCANCODE_RIGHT])
      APPLY_DELTA(-rate * dt, 0, 1, 0);
    if (in->keys[SDL_SCANCODE_UP])
      APPLY_DELTA(rate * dt, 1, 0, 0);
    if (in->keys[SDL_SCANCODE_DOWN])
      APPLY_DELTA(-rate * dt, 1, 0, 0);
    if (in->keys[SDL_SCANCODE_A])
      APPLY_DELTA(rate * dt, 0, 0, 1);
    if (in->keys[SDL_SCANCODE_D])
      APPLY_DELTA(-rate * dt, 0, 0, 1);

#undef APPLY_DELTA

    glm_quat_normalize(xform->rotation);
    ecs_modified(it->world, g_demo.model_entity, SafiTransform);
  }

  /* W / S → dolly camera */
  SafiCamera *cam = ecs_get_mut(it->world, g_demo.camera_entity, SafiCamera);
  if (cam) {
    if (in->keys[SDL_SCANCODE_W])
      cam->target[2] -= 2.0f * dt;
    if (in->keys[SDL_SCANCODE_S])
      cam->target[2] += 2.0f * dt;

    /* Update audio listener from the camera pose so 3D sounds pan/attenuate
     * relative to the viewer. Eye follows the same (target + Z=3) used by
     * the render system. */
    float eye_pos[3] = {cam->target[0], cam->target[1], cam->target[2] + 3.0f};
    float fwd[3] = {0, 0, -1};
    float up[3] = {0, 1, 0};
    safi_audio_set_listener(eye_pos, fwd, up);
  }

  /* Left click → raycast from the cursor into the scene, log what was hit.
   * Edge-detected so holding the button fires once per press. Clicks landing
   * on a MicroUI panel are ignored — otherwise the Scene/Inspector hierarchy
   * would fire a UI click sfx every time the user picks an entity. */
  mu_Context *mu = safi_debug_ui_context();
  bool over_panel = mu && mu->hover_root != NULL;

  static bool prev_lmb = false;
  bool lmb = in->mouse_buttons[1]; /* SDL_BUTTON_LEFT == 1 */
  if (lmb && !prev_lmb && cam && !over_panel) {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(SDL_GetKeyboardFocus(), &ww, &wh);
    if (ww > 0 && wh > 0) {
      /* Reconstruct the view/proj used by the render system (see
       * engine/src/render/render_system.c: eye = target + (0,0,3)). */
      vec3 eye = {cam->target[0], cam->target[1], cam->target[2] + 3.0f};
      vec3 center = {cam->target[0], cam->target[1], cam->target[2]};
      vec3 up = {0, 1, 0};
      mat4 view, proj, vp, inv_vp;
      glm_lookat(eye, center, up, view);
      glm_perspective(cam->fov_y_radians, (float)ww / (float)wh, cam->z_near,
                      cam->z_far, proj);
      glm_mat4_mul(proj, view, vp);
      glm_mat4_inv(vp, inv_vp);

      /* Pixel → NDC (flip Y — SDL's origin is top-left). */
      float nx = (2.0f * in->mouse_x / (float)ww) - 1.0f;
      float ny = 1.0f - (2.0f * in->mouse_y / (float)wh);

      /* Unproject a far-plane point, then build dir = normalize(p - eye). */
      vec4 ndc = {nx, ny, 1.0f, 1.0f};
      vec4 world;
      glm_mat4_mulv(inv_vp, ndc, world);
      vec3 far_pt = {world[0] / world[3], world[1] / world[3],
                     world[2] / world[3]};
      vec3 dir;
      glm_vec3_sub(far_pt, eye, dir);
      glm_vec3_normalize(dir);

      SafiRayHit hit;
      if (safi_physics_raycast(it->world, eye, dir, 100.0f, 0, &hit)) {
        const SafiName *n = ecs_get(it->world, hit.entity, SafiName);
        SAFI_LOG_INFO(
            "raycast hit: %s (entity %llu) at (%.2f, %.2f, %.2f) frac=%.2f",
            n ? n->value : "<unnamed>", (unsigned long long)hit.entity,
            hit.point[0], hit.point[1], hit.point[2], hit.fraction);
        /* 3D impact sfx at the hit point on the sfx bus. */
        if (g_demo.click_sfx.id)
          safi_audio_play_3d(g_demo.click_sfx, safi_audio_bus_sfx(), hit.point,
                             1.0f, 1.0f, false);
      } else {
        SAFI_LOG_INFO("raycast: miss");
        /* 2D UI click on miss. */
        if (g_demo.click_sfx.id)
          safi_audio_play(g_demo.click_sfx, safi_audio_bus_ui(), 0.7f, 1.0f,
                          false);
      }
    }
  }
  prev_lmb = lmb;

  /* F5 = save scene, F9 = reload scene. */
  if (in->keys_pressed[SDL_SCANCODE_F5]) {
    safi_scene_save(it->world, "scene.json");
  }
  if (in->keys_pressed[SDL_SCANCODE_F9]) {
    safi_scene_load(it->world, "scene.json");
  }
}
