/**
 * safi/ecs/stable_id.h — 128-bit random IDs for persistent entity identity.
 *
 * Every serializable entity carries a `SafiStableId` whose value is
 * preserved across scene save/load, snapshot/restore, and future prefab
 * instantiation. `SafiName` stays the human-readable label; renames never
 * break references because lookups key on the stable id instead.
 *
 * Format on disk: 32 lowercase hex characters (no dashes). The first 16
 * encode `hi`, the next 16 encode `lo`, both big-endian.
 */
#ifndef SAFI_ECS_STABLE_ID_H
#define SAFI_ECS_STABLE_ID_H

#include <stdbool.h>
#include <stdint.h>
#include <flecs.h>

#include "safi/ecs/components.h"

/* Generate a fresh random id. Seeds internally on first call from
 * `SDL_GetPerformanceCounter` + the process address so collisions are
 * vanishingly improbable within a session. */
SafiStableId safi_stable_id_new(void);

/* true when both halves are zero (used as the "invalid" sentinel). */
static inline bool safi_stable_id_is_zero(SafiStableId id) {
    return id.hi == 0 && id.lo == 0;
}

static inline bool safi_stable_id_equal(SafiStableId a, SafiStableId b) {
    return a.hi == b.hi && a.lo == b.lo;
}

/* Hex encoding/decoding. `out` must have room for 33 bytes; result is
 * zero-terminated. `from_string` returns false on malformed input. */
void safi_stable_id_to_string  (SafiStableId id, char out[33]);
bool safi_stable_id_from_string(const char *str, SafiStableId *out);

/* Find the single entity carrying a given id. Linear scan; the world has
 * a few hundred named entities in practice. O(1) lookup via an internal
 * hashmap is a future optimisation. */
ecs_entity_t safi_scene_find_entity_by_stable_id(ecs_world_t *world,
                                                  SafiStableId id);

/* Register an OnAdd observer on `SafiName` so every named entity picks up
 * a fresh `SafiStableId` automatically (skipped for entities already
 * carrying one or tagged `SafiEngineOwned`). Called once per world
 * inside `safi_register_builtin_components`. */
void safi_stable_id_install(ecs_world_t *world);

#endif /* SAFI_ECS_STABLE_ID_H */
