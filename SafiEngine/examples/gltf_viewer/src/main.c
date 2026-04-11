/*
 * gltf_viewer — SafiEngine demo.
 *
 * Loads a glTF model and lets the user rotate and zoom it with the keyboard:
 *
 *   Arrow Up/Down    → pitch
 *   Arrow Left/Right → yaw
 *   A / D            → roll
 *   W / S            → zoom (dolly along camera forward)
 *
 * A MicroUI overlay shows a Scene hierarchy and a component inspector.
 *
 * This file is intentionally thin — it just boots the engine, hands off to
 * scene_setup(), registers the per-frame systems, and runs the main loop.
 * Game logic lives in scene.c and the files under systems/.
 */
#include <safi/safi.h>

#include "demo_state.h"
#include "scene.h"
#include "systems/control_system.h"
#include "systems/render_system.h"

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  SafiApp app;
  SafiAppDesc desc = {
      .title = "SafiEngine — glTF Viewer",
      .width = 1920,
      .height = 1080,
      .vsync = true,
      .enable_debug_ui = true,
  };
  if (!safi_app_init(&app, &desc))
    return 1;

  if (!scene_setup(&app)) {
    safi_app_shutdown(&app);
    return 2;
  }

  ecs_world_t *world = safi_app_world(&app);

  /* Register user systems. The app pointer goes through ctx so
   * render_system can reach the renderer without a global. */
  ecs_system(world,
             {
                 .entity = ecs_entity(
                     world, {.name = "control_system",
                             .add = ecs_ids(ecs_dependson(EcsOnUpdate))}),
                 .callback = control_system,
             });
  ecs_system(world, {
                        .entity = ecs_entity(
                            world, {.name = "render_system",
                                    .add = ecs_ids(ecs_dependson(EcsOnStore))}),
                        .callback = render_system,
                        .ctx = &app,
                    });

  safi_app_run(&app);

  safi_model_destroy(&app.renderer, &g_demo.model);
  safi_app_shutdown(&app);
  return 0;
}
