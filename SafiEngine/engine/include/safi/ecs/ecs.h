/**
 * safi/ecs/ecs.h — thin wrapper over flecs.
 *
 * The engine uses flecs as its ECS. This header simply re-exports the flecs
 * world type and provides a handful of convenience macros so engine users
 * can write Bevy-style code in plain C.
 *
 *   ecs_world_t *world = safi_ecs_create();
 *   SAFI_COMPONENT(world, Transform);
 *   ECS_SYSTEM(world, spin_system, EcsOnUpdate, Transform, Spin);
 *
 * Full flecs documentation: https://www.flecs.dev
 */
#ifndef SAFI_ECS_ECS_H
#define SAFI_ECS_ECS_H

#include <flecs.h>

/* Create a new world preloaded with the engine's stock components and
 * resources. Most apps use safi_app_world() instead of calling this. */
ecs_world_t *safi_ecs_create(void);

/* Destroy a world created with safi_ecs_create. */
void safi_ecs_destroy(ecs_world_t *world);

/* Register a component type. Alias for ECS_COMPONENT_DEFINE kept for
 * readability. */
#define SAFI_COMPONENT(world, T) ECS_COMPONENT_DEFINE(world, T)

#endif /* SAFI_ECS_ECS_H */
