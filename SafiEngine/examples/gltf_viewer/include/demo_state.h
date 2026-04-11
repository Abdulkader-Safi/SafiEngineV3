/*
 * demo_state.h — Shared state for the gltf_viewer demo.
 *
 * The demo uses a single module-level struct that holds the loaded model and
 * handles for the entities spawned during scene setup. Systems look these up
 * to operate on the "main" model / camera rather than doing ECS queries.
 */
#ifndef GLTF_VIEWER_DEMO_STATE_H
#define GLTF_VIEWER_DEMO_STATE_H

#include <safi/safi.h>

typedef struct DemoState {
  SafiModel model;
  ecs_entity_t model_entity;
  ecs_entity_t camera_entity;
  ecs_entity_t sun_entity;
  ecs_entity_t sky_entity;
} DemoState;

extern DemoState g_demo;

#endif /* GLTF_VIEWER_DEMO_STATE_H */
