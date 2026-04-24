/**
 * safi/ecs/component_registry.h — central table of component metadata.
 *
 * Maps each registered component's ecs_id_t to its human-readable name,
 * serialize/deserialize callbacks (for scene files), and an inspector
 * draw callback. The serializer, inspector panel, and the future
 * "+ Add Component" editor menu all iterate this table.
 *
 * Stock components are registered during safi_register_builtin_components.
 * User components can be added with safi_component_registry_register.
 */
#ifndef SAFI_ECS_COMPONENT_REGISTRY_H
#define SAFI_ECS_COMPONENT_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <flecs.h>

/* Forward-declare types from libraries the callbacks reference so callers
 * don't have to pull in microui.h or cJSON.h if they only iterate. */
typedef struct mu_Context mu_Context;
typedef struct cJSON      cJSON;

/* ---- Callback signatures ------------------------------------------------ */

/* Serialize a component on entity `e` into a cJSON object. Return NULL
 * to indicate "nothing to write" (component not present, or empty). */
typedef cJSON *(*SafiSerializeFn)(ecs_world_t *world,
                                   ecs_entity_t entity,
                                   ecs_id_t     component_id);

/* Deserialize a cJSON object back into a component on entity `e`.
 * The function should call ecs_set_ptr / ecs_set internally. */
typedef void (*SafiDeserializeFn)(ecs_world_t *world,
                                   ecs_entity_t entity,
                                   const cJSON  *json);

/* Draw the inspector widgets for a component on entity `e`. The function
 * is called inside an open MicroUI window (after mu_header_ex). */
typedef void (*SafiInspectorFn)(mu_Context   *ctx,
                                 ecs_world_t  *world,
                                 ecs_entity_t  entity);

/* Construct the component on entity `e` with sensible defaults. Used by
 * the "+ Add Component" flow and by the entity-preset helpers. NULL means
 * "fall back to ecs_add_id with zero-initialised bytes" — fine for POD
 * components that are meaningful at {0}, not for anything needing a
 * specific default (e.g. SafiTransform wants identity scale). */
typedef void (*SafiDefaultInitFn)(ecs_world_t *world,
                                   ecs_entity_t entity,
                                   ecs_id_t     component_id);

/* ---- Component info ----------------------------------------------------- */

typedef struct SafiComponentInfo {
    ecs_id_t          id;           /* flecs component id */
    const char       *name;         /* "SafiTransform", "SafiCamera", … */
    size_t            size;         /* sizeof(T) */
    SafiSerializeFn   serialize;    /* NULL = not serializable */
    SafiDeserializeFn deserialize;  /* NULL = not serializable */
    SafiInspectorFn   draw;         /* NULL = no inspector row */
    SafiDefaultInitFn default_init; /* NULL = ecs_add_id + zero bytes */
    bool              serializable; /* convenience flag */
} SafiComponentInfo;

/* ---- Public API --------------------------------------------------------- */

void safi_component_registry_init(void);

/* Register a component. Takes a copy of the info struct. */
void safi_component_registry_register(const SafiComponentInfo *info);

/* Iteration. */
int                      safi_component_registry_count(void);
const SafiComponentInfo *safi_component_registry_get(int index);

/* Lookup by flecs id or by name string. Returns NULL if not found. */
const SafiComponentInfo *safi_component_registry_find(ecs_id_t id);
const SafiComponentInfo *safi_component_registry_find_by_name(const char *name);

/* Construct a component on `entity` with its registered default values.
 * Looks up by name, invokes `default_init` if set, otherwise `ecs_add_id`.
 * Returns false if no component with that name is registered. */
bool safi_component_registry_construct(ecs_world_t *world,
                                        ecs_entity_t entity,
                                        const char *name);

#endif /* SAFI_ECS_COMPONENT_REGISTRY_H */
