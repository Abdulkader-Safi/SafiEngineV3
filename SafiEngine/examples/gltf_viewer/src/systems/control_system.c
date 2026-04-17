#include "systems/control_system.h"
#include "demo_state.h"

#include <safi/safi.h>
#include <safi/ui/debug_ui.h>

#include <SDL3/SDL.h>
#include <cJSON.h>
#include <microui.h>

/* In-memory snapshot produced by F6 and consumed by F7. Owned here, freed
 * on the next F6 press or at process exit. Static because this is a
 * single-player demo scratchpad, not something the engine needs to track. */
static cJSON *g_control_snapshot = NULL;

/* Scene-load clears and recreates every named entity, so the cached
 * handles in g_demo go stale after F9. Re-look them up by name; names
 * the loaded scene doesn't contain are zeroed so the guards below skip
 * their branches instead of dereferencing a dead id. */
static void refresh_demo_handles(ecs_world_t *world) {
  g_demo.model_entity  = safi_scene_find_entity_by_name(world, "Model");
  g_demo.camera_entity = safi_scene_find_entity_by_name(world, "Camera");
  g_demo.sun_entity    = safi_scene_find_entity_by_name(world, "Sun");
  g_demo.sky_entity    = safi_scene_find_entity_by_name(world, "Sky");
}

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

  /* Rotate the model entity (arrows + A/D) regardless of selection.
   * Guard with ecs_is_alive — the handle can go stale if a scene load
   * replaced the world out from under us. */
  SafiTransform *xform = NULL;
  if (g_demo.model_entity && ecs_is_alive(it->world, g_demo.model_entity)) {
    xform = ecs_get_mut(it->world, g_demo.model_entity, SafiTransform);
  }
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
  SafiCamera *cam = NULL;
  if (g_demo.camera_entity && ecs_is_alive(it->world, g_demo.camera_entity)) {
    cam = ecs_get_mut(it->world, g_demo.camera_entity, SafiCamera);
  }
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
    if (safi_scene_load(it->world, "scene.json")) {
      refresh_demo_handles(it->world);
    }
  }

  /* F1 = toggle Edit/Play. Exercises the pipeline gate added in the
   * editor prerequisites: in Edit mode fixed-update is skipped, so the
   * falling cube freezes mid-air; flip back to Play and it resumes. */
  if (in->keys_pressed[SDL_SCANCODE_F1]) {
    SafiEditorMode m = safi_editor_get_mode(it->world);
    SafiEditorMode next =
        (m == SAFI_EDITOR_MODE_PLAY) ? SAFI_EDITOR_MODE_EDIT
                                     : SAFI_EDITOR_MODE_PLAY;
    safi_editor_set_mode(it->world, next);
    SAFI_LOG_INFO("editor: mode → %s",
                  next == SAFI_EDITOR_MODE_PLAY ? "Play" : "Edit");
  }

  /* F6 = snapshot the whole world into memory. F7 = restore that
   * snapshot onto the live entities (ids stay stable, components reset
   * to their snapshot values). */
  if (in->keys_pressed[SDL_SCANCODE_F6]) {
    if (g_control_snapshot) cJSON_Delete(g_control_snapshot);
    g_control_snapshot = safi_scene_snapshot_all(it->world);
    SAFI_LOG_INFO("editor: snapshot captured");
  }
  if (in->keys_pressed[SDL_SCANCODE_F7] && g_control_snapshot) {
    safi_scene_restore_snapshot(it->world, g_control_snapshot);
  }
}
