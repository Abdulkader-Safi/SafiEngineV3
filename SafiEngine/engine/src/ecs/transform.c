#include "safi/ecs/transform.h"
#include "safi/ecs/components.h"

#include <cglm/cglm.h>

/* Transform propagation: local → world, respecting EcsChildOf hierarchy.
 *
 * flecs' cascade traversal (EcsCascade on the parent term) visits
 * entities in EcsChildOf topological order, so by the time a child
 * appears in the iterator, its parent's SafiGlobalTransform has already
 * been written in this same tick. No recursion, no two-pass, no dirty
 * tracking — just one sequential sweep. */
static void transform_propagation_system(ecs_iter_t *it) {
    SafiTransform       *local  = ecs_field(it, SafiTransform, 0);
    SafiGlobalTransform *global = ecs_field(it, SafiGlobalTransform, 1);
    /* Parent's global transform. Null for root entities (those without
     * an EcsChildOf edge to another entity with SafiGlobalTransform). */
    SafiGlobalTransform *parent = ecs_field(it, SafiGlobalTransform, 2);

    for (int i = 0; i < it->count; i++) {
        mat4 local_mat;
        safi_transform_to_mat4(&local[i], local_mat);
        if (parent) {
            /* global_child = global_parent * local_child */
            glm_mat4_mul(parent->matrix, local_mat, global[i].matrix);
        } else {
            glm_mat4_copy(local_mat, global[i].matrix);
        }
    }
}

void safi_transform_register(ecs_world_t *world) {
    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "safi_transform_propagation",
            .add  = ecs_ids(ecs_dependson(EcsPreStore)),
        }),
        .query.terms = {
            { .id = ecs_id(SafiTransform),       .inout = EcsIn  },
            { .id = ecs_id(SafiGlobalTransform), .inout = EcsOut },
            { .id    = ecs_id(SafiGlobalTransform),
              .src   = { .id = EcsUp | EcsCascade },
              .trav  = EcsChildOf,
              .oper  = EcsOptional,
              .inout = EcsIn },
        },
        .callback = transform_propagation_system,
    });
}
