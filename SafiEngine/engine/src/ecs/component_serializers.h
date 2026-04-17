/**
 * component_serializers.h — internal header exposing the registration
 * function called from safi_register_builtin_components.
 */
#ifndef SAFI_ECS_COMPONENT_SERIALIZERS_H
#define SAFI_ECS_COMPONENT_SERIALIZERS_H

#include <flecs.h>

/* Registers serialize/deserialize/inspector callbacks for every stock
 * component into the component registry. Must be called after all
 * ECS_COMPONENT_DEFINE calls so ecs_id(T) is valid. */
void safi_register_builtin_component_info(ecs_world_t *world);

/* Register SafiRigidBody + SafiCollider — must be called after
 * safi_physics_init where their ECS_COMPONENT_DEFINE runs. */
void safi_register_physics_component_info(void);

#endif /* SAFI_ECS_COMPONENT_SERIALIZERS_H */
