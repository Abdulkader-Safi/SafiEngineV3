#include "safi/ecs/components.h"

ECS_COMPONENT_DECLARE(SafiTransform);
ECS_COMPONENT_DECLARE(SafiCamera);
ECS_COMPONENT_DECLARE(SafiMeshRenderer);
ECS_COMPONENT_DECLARE(SafiName);
ECS_COMPONENT_DECLARE(SafiSpin);
ECS_COMPONENT_DECLARE(SafiTime);
ECS_COMPONENT_DECLARE(SafiInput);

void safi_register_builtin_components(ecs_world_t *world) {
    ECS_COMPONENT_DEFINE(world, SafiTransform);
    ECS_COMPONENT_DEFINE(world, SafiCamera);
    ECS_COMPONENT_DEFINE(world, SafiMeshRenderer);
    ECS_COMPONENT_DEFINE(world, SafiName);
    ECS_COMPONENT_DEFINE(world, SafiSpin);
    ECS_COMPONENT_DEFINE(world, SafiTime);
    ECS_COMPONENT_DEFINE(world, SafiInput);
}
