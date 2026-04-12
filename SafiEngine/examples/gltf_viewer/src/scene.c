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
  snprintf(model_path, sizeof(model_path), "%s/models/BoxTextured.glb",
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
  ecs_set(world, falling, SafiRigidBody, {
      .type = SAFI_BODY_DYNAMIC,
      .mass = 1.0f,
      .friction = 0.5f,
      .restitution = 0.3f,
  });
  ecs_set(world, falling, SafiCollider, {
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
  ecs_set(world, ground, SafiRigidBody, {
      .type = SAFI_BODY_STATIC,
      .mass = 0.0f,
      .friction = 0.5f,
      .restitution = 0.3f,
  });
  ecs_set(world, ground, SafiCollider, {
      .shape = SAFI_COLLIDER_BOX,
      .box.half_extents = {5.0f, 0.1f, 5.0f},
  });
  ecs_set(world, ground, SafiName, {.value = "Ground"});

  return true;
}
