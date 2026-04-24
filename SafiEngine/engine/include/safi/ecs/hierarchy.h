/**
 * safi/ecs/hierarchy.h — safe wrappers around flecs' EcsChildOf.
 *
 * Raw `ecs_add_pair(world, child, EcsChildOf, parent)` works fine, but
 * nothing stops you from building a cycle (A childOf B, B childOf A) that
 * will blow up transform propagation and scene save. These helpers
 * centralise the one-liner with cycle rejection plus a detach-to-root
 * primitive for the editor's drag-reparent flow.
 */
#ifndef SAFI_ECS_HIERARCHY_H
#define SAFI_ECS_HIERARCHY_H

#include <stdbool.h>
#include <flecs.h>

/* Make `child` a descendant of `parent`. Returns false if the request
 * would create a cycle (i.e. `parent` is already at or below `child`
 * in the hierarchy) or if either entity is invalid. Passing `parent == 0`
 * is equivalent to `safi_entity_detach_from_parent(world, child)`. */
bool safi_entity_set_parent(ecs_world_t *world,
                            ecs_entity_t child,
                            ecs_entity_t parent);

/* Remove the EcsChildOf edge if any; no-op when the entity is already a
 * root. Child keeps all its other components. */
void safi_entity_detach_from_parent(ecs_world_t *world, ecs_entity_t child);

/* Fill `out` with up to `cap` direct children of `parent` and return the
 * number written. Children are returned in an unspecified order. */
int  safi_entity_children(ecs_world_t *world,
                          ecs_entity_t parent,
                          ecs_entity_t *out,
                          int cap);

#endif /* SAFI_ECS_HIERARCHY_H */
