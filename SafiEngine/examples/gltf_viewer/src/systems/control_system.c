#include "systems/control_system.h"
#include "demo_state.h"

#include <safi/safi.h>
#include <safi/ui/debug_ui.h>

#include <SDL3/SDL.h>
#include <microui.h>

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

/* Gameplay controls — runs on SafiGamePhase. Frozen in Edit/Paused by the
 * app scheduler, which is how the editor fly-cam and the cube controls
 * stop fighting over WASD. */
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

  /* W / S → dolly the gameplay camera. */
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
     * the render system's legacy fallback. */
    float eye_pos[3] = {cam->target[0], cam->target[1], cam->target[2] + 3.0f};
    float fwd[3] = {0, 0, -1};
    float up[3] = {0, 1, 0};
    safi_audio_set_listener(eye_pos, fwd, up);
  }

  /* Left click → raycast from the cursor into the scene, log what was hit.
   * Edge-detected so holding the button fires once per press. Clicks landing
   * on a MicroUI panel are ignored — otherwise the Scene/Inspector hierarchy
   * would fire a UI click sfx every time the user picks an entity. */
  static bool prev_lmb = false;
  bool lmb = in->mouse_buttons[1]; /* SDL_BUTTON_LEFT == 1 */
  if (lmb && !prev_lmb && cam && safi_debug_ui_mouse_over_viewport()) {
    int ww = 0, wh = 0;
    SDL_GetWindowSize(SDL_GetKeyboardFocus(), &ww, &wh);
    if (ww > 0 && wh > 0) {
      vec3 origin, dir;
      safi_camera_screen_ray(cam, ww, wh, in->mouse_x, in->mouse_y, origin,
                             dir);

      SafiRayHit hit;
      if (safi_physics_raycast(it->world, origin, dir, 100.0f, 0, &hit)) {
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
}

/* Scene file IO — runs on EcsOnUpdate so F5 and F9 work in Edit mode too.
 * F9 is demo-owned rather than engine-owned because the demo caches entity
 * ids (g_demo.model_entity etc.) that go stale after the scene is reloaded;
 * the refresh step below is what keeps the handle guards in control_system
 * from dereferencing dead ids. */
void scene_io_system(ecs_iter_t *it) {
  if (safi_debug_ui_wants_input())
    return;

  const SafiInput *in = ecs_singleton_get(it->world, SafiInput);
  if (!in)
    return;

  if (in->keys_pressed[SDL_SCANCODE_F5]) {
    safi_scene_save(it->world, "scene.json");
  }
  if (in->keys_pressed[SDL_SCANCODE_F9]) {
    if (safi_scene_load(it->world, "scene.json")) {
      refresh_demo_handles(it->world);
    }
  }
}
