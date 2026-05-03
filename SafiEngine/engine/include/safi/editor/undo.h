/**
 * safi/editor/undo.h — single-entity undo / redo ring buffer.
 *
 * Subscribes to the change bus on install. Each (entity, component) write
 * during Edit mode produces an undo step that captures the entity's state
 * before and after the write. Steps that share a non-zero group id (gizmo
 * drags wrap their per-frame writes in `safi_change_bus_begin_group` /
 * `_end_group`) collapse into a single step per entity for the entire
 * group, so a 60-frame drag is one undo, not sixty.
 *
 * Capped at 64 steps in each ring; oldest entries are evicted as the ring
 * wraps. Snapshots are stored as cJSON, reusing
 * `safi_scene_snapshot_entities` / `safi_scene_restore_snapshot`.
 *
 * Multi-entity edits (a group affecting N entities) currently produce N
 * sequential single-entity steps with the same group id. A future
 * "group-aware" undo can pop all steps in a group at once; for now each
 * Cmd-Z restores one entity at a time.
 */
#ifndef SAFI_EDITOR_UNDO_H
#define SAFI_EDITOR_UNDO_H

#include <flecs.h>

/* Subscribe to the change bus and seed the per-entity baseline cache from
 * every currently named entity. Idempotent: a second call is a no-op. */
void safi_undo_install(ecs_world_t *world);

/* Pop the top of the undo stack and apply its `before` snapshot. Pushes
 * the popped step onto the redo stack. No-op when the undo stack is
 * empty. */
void safi_undo_perform(ecs_world_t *world);

/* Pop the top of the redo stack and apply its `after` snapshot. Pushes
 * the popped step back onto the undo stack. No-op when the redo stack is
 * empty. */
void safi_undo_redo(ecs_world_t *world);

int safi_undo_depth(void);
int safi_undo_redo_depth(void);

/* Drop both rings and clear the per-entity cache. Useful around
 * scene_load / scene_clear, where entity ids change underfoot. */
void safi_undo_reset(void);

#endif /* SAFI_EDITOR_UNDO_H */
