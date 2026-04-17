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

#include <stdbool.h>
#include <flecs.h>

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

#endif /* SAFI_SCENE_SCENE_H */
