#include "safi/ecs/ecs.h"
#include "safi/ecs/components.h"

ecs_world_t *safi_ecs_create(void) {
    ecs_world_t *world = ecs_init();
    safi_register_builtin_components(world);
    return world;
}

void safi_ecs_destroy(ecs_world_t *world) {
    if (world) ecs_fini(world);
}
