#include "systems/control_system.h"
#include "demo_state.h"

#include <safi/safi.h>
#include <safi/ui/debug_ui.h>

#include <SDL3/SDL.h>

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
  }
}
