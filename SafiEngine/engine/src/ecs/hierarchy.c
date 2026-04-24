#include "safi/ecs/hierarchy.h"

/* Walk upward from `start` and return true if `target` is on the path.
 * Caps the climb at a generous depth so a bad hierarchy can't hang. */
static bool is_ancestor_of(ecs_world_t *world,
                           ecs_entity_t start,
                           ecs_entity_t target) {
    if (!start || !target) return false;
    ecs_entity_t cur = start;
    for (int depth = 0; depth < 1024; depth++) {
        ecs_entity_t p = ecs_get_target(world, cur, EcsChildOf, 0);
        if (!p) return false;
        if (p == target) return true;
        cur = p;
    }
    return false;
}

bool safi_entity_set_parent(ecs_world_t *world,
                            ecs_entity_t child,
                            ecs_entity_t parent) {
    if (!world || !child) return false;
    if (!ecs_is_alive(world, child)) return false;

    if (!parent) {
        safi_entity_detach_from_parent(world, child);
        return true;
    }
    if (!ecs_is_alive(world, parent)) return false;
    if (child == parent)              return false;
    /* Reject if `parent` is already a descendant of `child` — that would
     * close a loop and break the cascade traversal. */
    if (is_ancestor_of(world, parent, child)) return false;

    /* Drop any existing EcsChildOf edge before adding the new one so
     * flecs doesn't end up with two parents. */
    ecs_remove_pair(world, child, EcsChildOf, EcsWildcard);
    ecs_add_pair(world, child, EcsChildOf, parent);
    return true;
}

void safi_entity_detach_from_parent(ecs_world_t *world, ecs_entity_t child) {
    if (!world || !child || !ecs_is_alive(world, child)) return;
    ecs_remove_pair(world, child, EcsChildOf, EcsWildcard);
}

int safi_entity_children(ecs_world_t *world,
                         ecs_entity_t parent,
                         ecs_entity_t *out,
                         int cap) {
    if (!world || !parent || !out || cap <= 0) return 0;
    int written = 0;
    ecs_query_t *q = ecs_query(world, {
        .terms      = {{ .id = ecs_pair(EcsChildOf, parent) }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        for (int i = 0; i < it.count && written < cap; i++) {
            out[written++] = it.entities[i];
        }
    }
    ecs_query_fini(q);
    return written;
}
