/**
 * safi/scene/scene.h — JSON scene serialization.
 *
 * Save the current ECS world (all entities with SafiName) to a JSON file
 * and load it back. Components are serialized through the component
 * registry, so only registered + serializable components are written.
 *
 * The scene format is versioned (currently v1) and stores entity names,
 * parent relationships, and per-component data as nested JSON objects.
 */
#ifndef SAFI_SCENE_SCENE_H
#define SAFI_SCENE_SCENE_H

#include <stddef.h>
#include <stdbool.h>
#include <flecs.h>

/* Forward-declare cJSON so callers that only want save/load don't need
 * to drag cJSON's header into their translation unit. */
typedef struct cJSON cJSON;

/* Save every named entity in `world` to `path` as JSON.
 * Returns true on success. */
bool safi_scene_save(ecs_world_t *world, const char *path);

/* Delete every named entity in `world`, then load entities from `path`.
 * Returns true on success. Scene-clear happens before any loading — if
 * the file can't be parsed, the world is left empty. */
bool safi_scene_load(ecs_world_t *world, const char *path);

/* Delete every entity that carries SafiName. Singletons and engine-
 * internal entities are untouched. */
void safi_scene_clear(ecs_world_t *world);

/* ---- Snapshot / restore ------------------------------------------------- *
 *
 * Snapshot a live world (or a subset) to an in-memory JSON object, then
 * later restore component state onto the same entities. Unlike scene
 * save+load, restore does NOT destroy or re-create entities — it looks
 * up each snapshot entry by SafiName and mutates the existing entity in
 * place. This keeps ecs_entity_t handles stable across a Play→Stop
 * transition and avoids invalidating any pointers the rest of the engine
 * might be holding.
 *
 * Caller owns the returned cJSON* and must free it with cJSON_Delete. */

/* Snapshot a specific list of entities. Entities without SafiName are
 * skipped. Returns NULL on allocation failure. */
cJSON *safi_scene_snapshot_entities(ecs_world_t *world,
                                    const ecs_entity_t *ids, size_t count);

/* Snapshot every SafiName entity in the world. Shorthand for the common
 * Play-mode case where you want to restore the entire authored scene. */
cJSON *safi_scene_snapshot_all(ecs_world_t *world);

/* Restore component state from a snapshot onto existing, live entities,
 * matched by SafiName. Entities named in the snapshot that no longer
 * exist are skipped with a warning. Entities present in the world but
 * absent from the snapshot are left alone. Parent/child relationships
 * in the snapshot are ignored — reparenting during Play is out of scope
 * for M1. Returns true if the snapshot parsed cleanly. */
bool safi_scene_restore_snapshot(ecs_world_t *world, const cJSON *snapshot);

/* Look up a live entity by SafiName. Returns 0 if no match, or if the
 * name appears on an entity that is not alive. Useful after
 * safi_scene_load to refresh cached entity handles (loading clears and
 * recreates entities, invalidating any ids held by callers). */
ecs_entity_t safi_scene_find_entity_by_name(ecs_world_t *world,
                                            const char *name);

#endif /* SAFI_SCENE_SCENE_H */
