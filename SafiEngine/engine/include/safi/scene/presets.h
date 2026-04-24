/**
 * safi/scene/presets.h — factory helpers for common entity archetypes.
 *
 * The editor's "Create" menu (M5) calls these instead of hand-rolling
 * ecs_set blocks. Every preset:
 *
 *   1. Creates a new entity with `ecs_new`.
 *   2. Assigns `SafiName` (the OnAdd observer on `SafiName` auto-
 *      attaches a fresh `SafiStableId`).
 *   3. Invokes the per-component `default_init` callbacks from the
 *      component registry — that way the defaults live with the
 *      components, not scattered across one-off presets.
 *
 * Callers that want to tweak a preset (e.g. pick a mesh handle) can call
 * `ecs_set` on the returned entity after construction.
 */
#ifndef SAFI_SCENE_PRESETS_H
#define SAFI_SCENE_PRESETS_H

#include <stdint.h>
#include <flecs.h>

#include "safi/render/assets.h"
#include "safi/render/primitive_mesh.h"

ecs_entity_t safi_preset_empty       (ecs_world_t *world, const char *name);
ecs_entity_t safi_preset_mesh        (ecs_world_t *world, const char *name,
                                      SafiModelHandle model);
ecs_entity_t safi_preset_primitive   (ecs_world_t *world, const char *name,
                                      SafiPrimitiveShape shape);
ecs_entity_t safi_preset_directional_light(ecs_world_t *world, const char *name);
ecs_entity_t safi_preset_point_light (ecs_world_t *world, const char *name);
ecs_entity_t safi_preset_spot_light  (ecs_world_t *world, const char *name);
ecs_entity_t safi_preset_rect_light  (ecs_world_t *world, const char *name);
ecs_entity_t safi_preset_sky_light   (ecs_world_t *world, const char *name);
ecs_entity_t safi_preset_camera      (ecs_world_t *world, const char *name);
ecs_entity_t safi_preset_static_box  (ecs_world_t *world, const char *name,
                                      float half_x, float half_y, float half_z);
ecs_entity_t safi_preset_dynamic_sphere(ecs_world_t *world, const char *name,
                                        float radius, float mass);

#endif /* SAFI_SCENE_PRESETS_H */
