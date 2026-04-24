/**
 * safi/ecs/change_bus.h — central stream of "entity/component was set".
 *
 * The M6 undo/redo work reads from this bus. Gizmo drags coalesce into
 * one event pair per interaction via the begin_group/end_group helpers —
 * subscribers see a single (entity, component) entry for the whole drag
 * instead of one per frame.
 *
 * Any component registered with `serializable=true` is observed
 * automatically. Engine-internal writes (physics sync, transform
 * propagation) go through the same path but are out of scope for undo;
 * subscribers that care can filter on the component id.
 */
#ifndef SAFI_ECS_CHANGE_BUS_H
#define SAFI_ECS_CHANGE_BUS_H

#include <stdbool.h>
#include <stdint.h>
#include <flecs.h>

typedef struct SafiChange {
    ecs_entity_t entity;
    ecs_id_t     component;
    /* Monotonic frame counter so subscribers can coalesce "same entity +
     * component" entries within a single frame or group window. */
    uint64_t     frame;
    /* Non-zero during a begin_group/end_group window; zero for
     * individual inspector edits. */
    uint64_t     group_id;
} SafiChange;

typedef void (*SafiChangeFn)(const SafiChange *change, void *ctx);

/* Register observers on every serializable component. Safe to call once
 * per world after `safi_register_builtin_components` has finished. */
void safi_change_bus_install(ecs_world_t *world);

/* Subscribe a callback. Up to 8 subscribers. The callback fires on the
 * main thread from inside an `ecs_progress` tick. */
bool safi_change_bus_subscribe(SafiChangeFn cb, void *ctx);
void safi_change_bus_unsubscribe(SafiChangeFn cb, void *ctx);

/* Bump the bus frame counter. Call once per tick from the app loop. */
void safi_change_bus_advance_frame(uint64_t frame);

/* Begin/end a group window — every change emitted in between carries the
 * same non-zero `group_id`, enabling subscribers to treat a drag or batch
 * edit as one undo step. Nesting is flat (not a stack); calling begin
 * twice keeps the same active group. */
void safi_change_bus_begin_group(void);
void safi_change_bus_end_group(void);

#endif /* SAFI_ECS_CHANGE_BUS_H */
