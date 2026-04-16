#include "scene.h"
#include "demo_state.h"

#include <safi/safi.h>
#include <safi/ui/debug_ui.h>

#include <stdio.h>

bool scene_setup(SafiApp *app) {
  /* Load the glTF model with all primitives and per-material textures.
   * Compiled shaders live in SAFI_DEMO_SHADER_DIR (see CMakeLists.txt);
   * the loader picks .spv or .msl based on the active GPU backend. */
  char model_path[1024];
  snprintf(model_path, sizeof(model_path), "%s/models/player.glb",
           SAFI_DEMO_ASSET_DIR);

  if (!safi_model_load_lit(&app->renderer, model_path, SAFI_DEMO_SHADER_DIR,
                           &g_demo.model)) {
    SAFI_LOG_ERROR("failed to load %s", model_path);
    return false;
  }

  ecs_world_t *world = safi_app_world(app);

  /* Spawn the model entity. */
  g_demo.model_entity = ecs_new(world);
  ecs_set(world, g_demo.model_entity, SafiTransform,
          {
              .position = {0.0f, 0.0f, 0.0f},
              .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
              .scale = {1.0f, 1.0f, 1.0f},
          });
  ecs_set(world, g_demo.model_entity, SafiGlobalTransform, {0});
  ecs_set(world, g_demo.model_entity, SafiMeshRenderer,
          {.model = &g_demo.model, .visible = true});
  ecs_set(world, g_demo.model_entity, SafiName, {.value = "Model"});

  /* Spawn the camera. */
  g_demo.camera_entity = ecs_new(world);
  ecs_set(world, g_demo.camera_entity, SafiCamera,
          {
              .fov_y_radians = 1.0472f, /* 60° */
              .z_near = 0.1f,
              .z_far = 100.0f,
              .target = {0, 0, 0},
          });
  ecs_set(world, g_demo.camera_entity, SafiActiveCamera, {0});
  ecs_set(world, g_demo.camera_entity, SafiName, {.value = "Camera"});

  /* Spawn a directional light (sun). */
  g_demo.sun_entity = ecs_new(world);
  ecs_set(world, g_demo.sun_entity, SafiDirectionalLight,
          {
              .direction = {-0.3f, -0.8f, -0.5f},
              .color = {1.0f, 1.0f, 0.9f},
              .intensity = 1.2f,
          });
  ecs_set(world, g_demo.sun_entity, SafiName, {.value = "Sun"});

  /* Spawn a sky light (ambient). */
  g_demo.sky_entity = ecs_new(world);
  ecs_set(world, g_demo.sky_entity, SafiSkyLight,
          {
              .color = {0.25f, 0.28f, 0.35f},
              .intensity = 1.0f,
          });
  ecs_set(world, g_demo.sky_entity, SafiName, {.value = "Sky"});

  /* Default selection for the inspector. */
  safi_debug_ui_select_entity(g_demo.model_entity);

  /* Dynamic box that falls under gravity and lands on the ground.
   * Parented under Model so it appears in the Scene tree, but physics
   * operates on its world-space SafiTransform (root-entity assumption). */
  ecs_entity_t falling = ecs_new(world);
  ecs_set(world, falling, SafiTransform,
          {
              .position = {0.0f, 3.0f, 0.0f},
              .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
              .scale = {0.3f, 0.3f, 0.3f},
          });
  ecs_set(world, falling, SafiGlobalTransform, {0});
  ecs_set(world, falling, SafiMeshRenderer,
          {.model = &g_demo.model, .visible = true});
  ecs_set(world, falling, SafiRigidBody,
          {
              .type = SAFI_BODY_DYNAMIC,
              .mass = 1.0f,
              .friction = 0.5f,
              .restitution = 0.3f,
          });
  ecs_set(world, falling, SafiCollider,
          {
              .shape = SAFI_COLLIDER_BOX,
              .box.half_extents = {0.15f, 0.15f, 0.15f},
          });
  ecs_set(world, falling, SafiName, {.value = "FallingBox"});

  /* Static ground plane — thin box. */
  ecs_entity_t ground = ecs_new(world);
  ecs_set(world, ground, SafiTransform,
          {
              .position = {0.0f, -2.0f, 0.0f},
              .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
              .scale = {1.0f, 1.0f, 1.0f},
          });
  ecs_set(world, ground, SafiGlobalTransform, {0});
  ecs_set(world, ground, SafiRigidBody,
          {
              .type = SAFI_BODY_STATIC,
              .mass = 0.0f,
              .friction = 0.5f,
              .restitution = 0.3f,
          });
  ecs_set(world, ground, SafiCollider,
          {
              .shape = SAFI_COLLIDER_BOX,
              .box.half_extents = {5.0f, 0.1f, 5.0f},
          });
  ecs_set(world, ground, SafiName, {.value = "Ground"});

  /* ---- Procedural primitives ------------------------------------------- *
   * Four shapes spread across the scene. The primitive_system builds their
   * GPU resources on EcsPreStore next frame and attaches SafiMeshRenderer
   * automatically; inspector edits rebuild the mesh on demand. */
  ecs_entity_t prim_box = ecs_new(world);
  ecs_set(world, prim_box, SafiTransform,
          {
              .position = {-2.0f, 0.0f, -1.0f},
              .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
              .scale = {1.0f, 1.0f, 1.0f},
          });
  ecs_set(world, prim_box, SafiGlobalTransform, {0});
  ecs_set(world, prim_box, SafiPrimitive,
          {
              .shape = SAFI_PRIMITIVE_BOX,
              .dims = {.box = {.half_extents = {0.4f, 0.4f, 0.4f}}},
              .color = {0.9f, 0.2f, 0.2f, 1.0f},
          });
  ecs_set(world, prim_box, SafiName, {.value = "Box"});

  ecs_entity_t prim_sphere = ecs_new(world);
  ecs_set(world, prim_sphere, SafiTransform,
          {
              .position = {2.0f, 0.0f, -1.0f},
              .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
              .scale = {1.0f, 1.0f, 1.0f},
          });
  ecs_set(world, prim_sphere, SafiGlobalTransform, {0});
  ecs_set(world, prim_sphere, SafiPrimitive,
          {
              .shape = SAFI_PRIMITIVE_SPHERE,
              .dims = {.sphere = {.radius = 0.5f, .segments = 24, .rings = 16}},
              .color = {0.2f, 0.4f, 0.9f, 1.0f},
          });
  ecs_set(world, prim_sphere, SafiName, {.value = "Sphere"});

  ecs_entity_t prim_plane = ecs_new(world);
  ecs_set(world, prim_plane, SafiTransform,
          {
              .position = {0.0f, -1.0f, 0.0f},
              .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
              .scale = {1.0f, 1.0f, 1.0f},
          });
  ecs_set(world, prim_plane, SafiGlobalTransform, {0});
  ecs_set(world, prim_plane, SafiPrimitive,
          {
              .shape = SAFI_PRIMITIVE_PLANE,
              .dims = {.plane = {.size = 4.0f}},
              .color = {0.25f, 0.55f, 0.25f, 1.0f},
          });
  ecs_set(world, prim_plane, SafiName, {.value = "PlaneFloor"});

  ecs_entity_t prim_capsule = ecs_new(world);
  ecs_set(world, prim_capsule, SafiTransform,
          {
              .position = {0.0f, 0.5f, -2.0f},
              .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
              .scale = {1.0f, 1.0f, 1.0f},
          });
  ecs_set(world, prim_capsule, SafiGlobalTransform, {0});
  ecs_set(world, prim_capsule, SafiPrimitive,
          {
              .shape = SAFI_PRIMITIVE_CAPSULE,
              .dims = {.capsule = {.radius = 0.3f,
                                   .height = 0.8f,
                                   .segments = 16,
                                   .rings = 8}},
              .color = {0.9f, 0.9f, 0.95f, 1.0f},
          });
  ecs_set(world, prim_capsule, SafiName, {.value = "Capsule"});

  /* ---- Audio ------------------------------------------------------------ */
  char audio_path[1024];
  snprintf(audio_path, sizeof(audio_path), "%s/audio/click.wav",
           SAFI_DEMO_ASSET_DIR);
  g_demo.click_sfx = safi_audio_load(audio_path, SAFI_AUDIO_LOAD_DECODE);

  snprintf(audio_path, sizeof(audio_path), "%s/audio/ambient.wav",
           SAFI_DEMO_ASSET_DIR);
  g_demo.ambient_music = safi_audio_load(audio_path, SAFI_AUDIO_LOAD_STREAM);
  if (g_demo.ambient_music.id) {
    safi_audio_play(g_demo.ambient_music, safi_audio_bus_music(),
                    /*volume*/ 0.3f, /*pitch*/ 1.0f, /*looping*/ true);
    SAFI_LOG_INFO("audio: ambient music looping on music bus at 30%%");
  }

  return true;
}
