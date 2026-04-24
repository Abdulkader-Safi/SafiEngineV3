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
      /* Demo assets live under SAFI_DEMO_ASSET_DIR (set by CMake). Using
       * it as the project root means scene files can reference
       * "models/player.glb" instead of the absolute build-machine path. */
      .project_root = SAFI_DEMO_ASSET_DIR,
  };
  if (!safi_app_init(&app, &desc))
    return 1;

  if (!scene_setup(&app)) {
    safi_app_shutdown(&app);
    return 2;
  }

  ecs_world_t *world = safi_app_world(&app);

  /* The demo has no Play/Pause toolbar yet, so opt into Play mode at
   * startup — otherwise the falling cube would stay frozen in the default
   * Edit mode. Press F1 at runtime to toggle Edit ↔ Play. */
  safi_editor_set_mode(world, SAFI_EDITOR_MODE_PLAY);

  /* Register user systems. The engine's render system is registered
   * automatically by safi_app_init — only game logic goes here.
   *
   * control_system runs on SafiGamePhase so it is skipped while the editor
   * is in Edit or Paused mode. That way WASD/arrow input drives the model
   * only while the game is actually playing — in Edit mode those keys
   * belong to the editor fly-cam. scene_io_system stays on EcsOnUpdate
   * because F5 save / F9 load need to work regardless of mode. */
  ecs_system(world,
             {
                 .entity = ecs_entity(
                     world, {.name = "control_system",
                             .add = ecs_ids(ecs_dependson(SafiGamePhase))}),
                 .callback = control_system,
             });
  ecs_system(world,
             {
                 .entity = ecs_entity(
                     world, {.name = "scene_io_system",
                             .add = ecs_ids(ecs_dependson(EcsOnUpdate))}),
                 .callback = scene_io_system,
             });
  ecs_system(world,
             {
                 .entity = ecs_entity(
                     world, {.name = "music_gate_system",
                             .add = ecs_ids(ecs_dependson(EcsOnUpdate))}),
                 .callback = music_gate_system,
             });

  safi_app_run(&app);

  safi_app_shutdown(&app);
  return 0;
}
