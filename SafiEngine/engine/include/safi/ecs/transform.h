/**
 * safi/ecs/transform.h — hierarchy propagation system.
 *
 * SafiGlobalTransform is written every frame by an engine-owned system
 * that walks the EcsChildOf tree parents-before-children using flecs'
 * cascade traversal. The system runs on EcsPostUpdate, so it sees any
 * SafiTransform mutations from OnUpdate in the same tick and the render
 * stage reads a fully-settled world.
 *
 * safi_ecs_create() calls safi_transform_register automatically. Apps
 * that bring their own flecs world can call it directly.
 */
#ifndef SAFI_ECS_TRANSFORM_H
#define SAFI_ECS_TRANSFORM_H

#include <flecs.h>

/* Register the transform propagation system on the given world. The
 * SafiTransform and SafiGlobalTransform components must already be
 * defined (i.e. call safi_register_builtin_components first). */
void safi_transform_register(ecs_world_t *world);

#endif /* SAFI_ECS_TRANSFORM_H */
