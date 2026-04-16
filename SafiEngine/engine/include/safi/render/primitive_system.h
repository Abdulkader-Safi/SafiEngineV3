/**
 * safi/render/primitive_system.h — engine-owned system that builds GPU
 * resources for entities carrying a SafiPrimitive component.
 *
 * Registered automatically by safi_app_init. The system runs on EcsPreStore
 * each frame, diffs every SafiPrimitive against its last-built snapshot, and
 * rebuilds the mesh / texture / material / wrapped-model whenever any
 * user-facing field changes. It also auto-attaches a SafiMeshRenderer
 * pointing at the procedural model so the standard render system picks it
 * up without special-casing.
 *
 * A companion EcsOnRemove observer frees the GPU resources when the
 * component is removed or its entity is destroyed.
 */
#ifndef SAFI_RENDER_PRIMITIVE_SYSTEM_H
#define SAFI_RENDER_PRIMITIVE_SYSTEM_H

#include <flecs.h>

#include "safi/core/app.h"

void safi_primitive_system_init(ecs_world_t *world, SafiApp *app);

#endif /* SAFI_RENDER_PRIMITIVE_SYSTEM_H */
