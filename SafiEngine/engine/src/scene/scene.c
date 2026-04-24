/*
 * scene.c — JSON scene save / load / clear.
 *
 * Writer: queries all entities with SafiName, iterates the component
 * registry for each, calls serialize callbacks, writes JSON via cJSON.
 *
 * Reader: three-pass approach:
 *   1. Create entities + set SafiName (build a name→entity map).
 *   2. Deserialize each component via registry callbacks.
 *   3. Wire parent/child relationships via EcsChildOf.
 */
#include "safi/scene/scene.h"
#include "safi/ecs/components.h"
#include "safi/ecs/component_registry.h"
#include "safi/ecs/hierarchy.h"
#include "safi/ecs/stable_id.h"
#include "safi/editor/editor_camera.h"
#include "safi/core/log.h"

#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SCENE_VERSION 1
#define MAX_ENTITIES  1024

/* ---- String pool -------------------------------------------------------- *
 * SafiName.value points at a const char* that must outlive the entity.
 * When loading from JSON we strdup each name into a small arena that
 * persists until the next safi_scene_clear or safi_scene_load. */

static char *g_name_pool[MAX_ENTITIES];
static int   g_name_pool_count = 0;

static const char *s_pool_string(const char *src) {
    if (g_name_pool_count >= MAX_ENTITIES) return src;
    char *dup = strdup(src);
    g_name_pool[g_name_pool_count++] = dup;
    return dup;
}

static void s_pool_free(void) {
    for (int i = 0; i < g_name_pool_count; i++) free(g_name_pool[i]);
    g_name_pool_count = 0;
}

/* ---- Per-entity serializers (shared by save + snapshot) ----------------- */

/* Serialize `e` into a fresh cJSON object of the shape
 *     { "name": ..., "parent"?: ..., "components": { ... } }
 * Returns NULL if the entity has no SafiName (we never serialize unnamed
 * entities — names are the stable reference used across save/load). */
static cJSON *serialize_entity(ecs_world_t *world, ecs_entity_t e) {
    const SafiName *name = ecs_get(world, e, SafiName);
    if (!name || !name->value) return NULL;

    /* Engine infrastructure (editor fly-cam, etc.) stays out of scene
     * save and Play/Stop snapshots — otherwise restoring their state
     * across a Play/Stop cycle double-tags things like SafiActiveCamera
     * and the fly-cam stops responding in Edit mode. The SafiEditorCamera
     * check is redundant once SafiEngineOwned is universally applied but
     * kept as a belt-and-suspenders for migration. */
    if (ecs_has(world, e, SafiEngineOwned)) return NULL;
    if (ecs_has(world, e, SafiEditorCamera)) return NULL;

    cJSON *ej = cJSON_CreateObject();
    cJSON_AddStringToObject(ej, "name", name->value);

    /* Stable id is the authoritative lookup key for snapshot restore and
     * future prefab references; name is just a label. The OnAdd observer
     * on SafiName guarantees every serializable entity has one. */
    const SafiStableId *sid = ecs_get(world, e, SafiStableId);
    if (sid) {
        char hex[33];
        safi_stable_id_to_string(*sid, hex);
        cJSON_AddStringToObject(ej, "stable_id", hex);
    }

    ecs_entity_t parent = ecs_get_target(world, e, EcsChildOf, 0);
    if (parent) {
        const SafiName *pn = ecs_get(world, parent, SafiName);
        if (pn && pn->value)
            cJSON_AddStringToObject(ej, "parent", pn->value);
    }

    cJSON *comps = cJSON_CreateObject();
    int reg_count = safi_component_registry_count();
    for (int c = 0; c < reg_count; c++) {
        const SafiComponentInfo *ci = safi_component_registry_get(c);
        if (!ci->serializable || !ci->serialize) continue;
        if (!ci->id) continue;                       /* not yet registered */
        if (ci->id == ecs_id(SafiName)) continue;    /* already at top level */
        if (!ecs_has_id(world, e, ci->id)) continue;

        cJSON *cj = ci->serialize(world, e, ci->id);
        if (cj) cJSON_AddItemToObject(comps, ci->name, cj);
    }
    cJSON_AddItemToObject(ej, "components", comps);
    return ej;
}

/* Deserialize the "components" object of `entry` onto an existing entity.
 * Does not touch name or parent — those are handled by the caller because
 * scene load and snapshot restore want different policies. */
static void deserialize_entity_components(ecs_world_t *world, ecs_entity_t e,
                                          const cJSON *entry) {
    const cJSON *comps = cJSON_GetObjectItem(entry, "components");
    if (!comps) return;

    const cJSON *cj = NULL;
    cJSON_ArrayForEach(cj, comps) {
        const char *comp_name = cj->string;
        const SafiComponentInfo *ci =
            safi_component_registry_find_by_name(comp_name);
        if (!ci || !ci->deserialize) {
            const SafiName *nm = ecs_get(world, e, SafiName);
            SAFI_LOG_WARN("scene: unknown component '%s' on entity '%s'",
                          comp_name, (nm && nm->value) ? nm->value : "<unnamed>");
            continue;
        }
        ci->deserialize(world, e, cj);
    }
}

/* ---- Save --------------------------------------------------------------- */

bool safi_scene_save(ecs_world_t *world, const char *path) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", SCENE_VERSION);
    cJSON *entities = cJSON_AddArrayToObject(root, "entities");

    ecs_query_t *q = ecs_query(world, {
        .terms = {{ .id = ecs_id(SafiName) }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        for (int i = 0; i < it.count; i++) {
            cJSON *ej = serialize_entity(world, it.entities[i]);
            if (ej) cJSON_AddItemToArray(entities, ej);
        }
    }
    ecs_query_fini(q);

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json_str) {
        SAFI_LOG_ERROR("scene: cJSON_Print failed");
        return false;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        SAFI_LOG_ERROR("scene: can't open '%s' for writing", path);
        free(json_str);
        return false;
    }
    fputs(json_str, f);
    fclose(f);
    free(json_str);

    SAFI_LOG_INFO("scene: saved to '%s'", path);
    return true;
}

/* ---- Clear -------------------------------------------------------------- */

void safi_scene_clear(ecs_world_t *world) {
    /* Collect entity IDs first, then delete — deleting during iteration
     * invalidates the iterator. Entities tagged SafiEngineOwned (editor
     * fly-cam and other engine infrastructure) are kept so a scene reload
     * doesn't tear down the tool substrate. */
    ecs_entity_t buf[MAX_ENTITIES];
    int count = 0;

    ecs_query_t *q = ecs_query(world, {
        .terms = {{ .id = ecs_id(SafiName) }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        for (int i = 0; i < it.count && count < MAX_ENTITIES; i++) {
            if (ecs_has(world, it.entities[i], SafiEngineOwned)) continue;
            buf[count++] = it.entities[i];
        }
    }
    ecs_query_fini(q);

    for (int i = 0; i < count; i++)
        ecs_delete(world, buf[i]);

    s_pool_free();
    SAFI_LOG_INFO("scene: cleared %d entities", count);
}

/* ---- Load --------------------------------------------------------------- */

bool safi_scene_load(ecs_world_t *world, const char *path) {
    /* Read file. */
    FILE *f = fopen(path, "r");
    if (!f) {
        SAFI_LOG_ERROR("scene: can't open '%s' for reading", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        SAFI_LOG_ERROR("scene: JSON parse failed for '%s'", path);
        return false;
    }

    cJSON *version = cJSON_GetObjectItem(root, "version");
    if (!version || version->valueint != SCENE_VERSION) {
        SAFI_LOG_WARN("scene: version mismatch (expected %d, got %d)",
                      SCENE_VERSION, version ? version->valueint : -1);
    }

    cJSON *entities = cJSON_GetObjectItem(root, "entities");
    if (!entities || !cJSON_IsArray(entities)) {
        SAFI_LOG_ERROR("scene: no 'entities' array");
        cJSON_Delete(root);
        return false;
    }

    /* Clear previous scene + string pool. */
    safi_scene_clear(world);

    /* --- Pass 1: create entities + names, build name→entity map --- */
    typedef struct { const char *name; ecs_entity_t entity; } NameEntry;
    int entity_count = cJSON_GetArraySize(entities);
    NameEntry *map = (NameEntry *)calloc((size_t)entity_count, sizeof(NameEntry));

    for (int i = 0; i < entity_count; i++) {
        cJSON *ej = cJSON_GetArrayItem(entities, i);
        cJSON *nj = cJSON_GetObjectItem(ej, "name");
        const char *name = (nj && cJSON_IsString(nj)) ? nj->valuestring : "unnamed";

        ecs_entity_t e = ecs_new(world);
        const char *pooled = s_pool_string(name);
        ecs_set(world, e, SafiName, { .value = pooled });
        /* Setting SafiName triggers the auto-stable-id observer, which
         * generates a fresh id. If the JSON has one, overwrite. Scene
         * files produced before this change simply keep the auto id. */
        cJSON *sidj = cJSON_GetObjectItem(ej, "stable_id");
        if (sidj && cJSON_IsString(sidj)) {
            SafiStableId parsed;
            if (safi_stable_id_from_string(sidj->valuestring, &parsed)) {
                ecs_set_ptr(world, e, SafiStableId, &parsed);
            } else {
                SAFI_LOG_WARN("scene: malformed stable_id '%s' on '%s'; "
                              "using auto-generated id instead",
                              sidj->valuestring, pooled);
            }
        }

        map[i].name   = pooled;
        map[i].entity = e;
    }

    /* --- Pass 2: deserialize components --- */
    for (int i = 0; i < entity_count; i++) {
        cJSON *ej = cJSON_GetArrayItem(entities, i);
        deserialize_entity_components(world, map[i].entity, ej);

        /* Ensure GlobalTransform is present if Transform was deserialized,
         * so the propagation system writes the world matrix. */
        if (ecs_has(world, map[i].entity, SafiTransform) &&
            !ecs_has(world, map[i].entity, SafiGlobalTransform)) {
            ecs_set(world, map[i].entity, SafiGlobalTransform, {0});
        }
    }

    /* --- Pass 3: wire parent/child via the safe wrapper so hand-edited
     * JSON files can't accidentally close a cycle --- */
    for (int i = 0; i < entity_count; i++) {
        cJSON *ej = cJSON_GetArrayItem(entities, i);
        cJSON *pj = cJSON_GetObjectItem(ej, "parent");
        if (!pj || !cJSON_IsString(pj)) continue;

        const char *parent_name = pj->valuestring;
        for (int j = 0; j < entity_count; j++) {
            if (strcmp(map[j].name, parent_name) == 0) {
                if (!safi_entity_set_parent(world, map[i].entity, map[j].entity)) {
                    SAFI_LOG_WARN("scene: rejected parent '%s' -> '%s' (cycle?)",
                                  parent_name, map[i].name);
                }
                break;
            }
        }
    }

    free(map);
    cJSON_Delete(root);

    SAFI_LOG_INFO("scene: loaded %d entities from '%s'", entity_count, path);
    return true;
}

/* ---- Snapshot / restore ------------------------------------------------- *
 *
 * Snapshots share the on-disk JSON shape so the same registry callbacks
 * and the same per-entity helpers drive both paths. Restore diverges
 * from load: it matches by SafiName onto *existing* entities and never
 * creates or deletes, so entity ids remain stable across Play→Stop. */

/* Build a cJSON root with { version, entities: [...] } given a prebuilt
 * entity-array. Takes ownership of `entities`. */
static cJSON *build_snapshot_root(cJSON *entities) {
    cJSON *root = cJSON_CreateObject();
    if (!root) { cJSON_Delete(entities); return NULL; }
    cJSON_AddNumberToObject(root, "version", SCENE_VERSION);
    cJSON_AddItemToObject(root, "entities", entities);
    return root;
}

cJSON *safi_scene_snapshot_entities(ecs_world_t *world,
                                    const ecs_entity_t *ids, size_t count) {
    cJSON *entities = cJSON_CreateArray();
    if (!entities) return NULL;

    for (size_t i = 0; i < count; i++) {
        cJSON *ej = serialize_entity(world, ids[i]);
        if (ej) cJSON_AddItemToArray(entities, ej);
    }
    return build_snapshot_root(entities);
}

cJSON *safi_scene_snapshot_all(ecs_world_t *world) {
    cJSON *entities = cJSON_CreateArray();
    if (!entities) return NULL;

    ecs_query_t *q = ecs_query(world, {
        .terms = {{ .id = ecs_id(SafiName) }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        for (int i = 0; i < it.count; i++) {
            cJSON *ej = serialize_entity(world, it.entities[i]);
            if (ej) cJSON_AddItemToArray(entities, ej);
        }
    }
    ecs_query_fini(q);
    return build_snapshot_root(entities);
}

/* Look up a live entity by SafiName. Linear scan; the world has tens of
 * named entities in practice, so the O(n*m) inside restore is fine.
 *
 * Care: stopping a flecs query iterator early leaks a stack-allocator
 * cursor that later asserts on ecs_fini. Call ecs_iter_fini on the early
 * match path so the iterator is released cleanly. */
ecs_entity_t safi_scene_find_entity_by_name(ecs_world_t *world, const char *name) {
    ecs_query_t *q = ecs_query(world, {
        .terms = {{ .id = ecs_id(SafiName) }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_entity_t found = 0;
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        SafiName *names = ecs_field(&it, SafiName, 0);
        for (int i = 0; i < it.count; i++) {
            if (names[i].value && strcmp(names[i].value, name) == 0) {
                found = it.entities[i];
                ecs_iter_fini(&it);
                goto done;
            }
        }
    }
done:
    ecs_query_fini(q);
    return found;
}

bool safi_scene_restore_snapshot(ecs_world_t *world, const cJSON *snapshot) {
    if (!snapshot) return false;

    const cJSON *entities = cJSON_GetObjectItem(snapshot, "entities");
    if (!entities || !cJSON_IsArray(entities)) {
        SAFI_LOG_ERROR("scene: snapshot missing 'entities' array");
        return false;
    }

    int restored = 0, missing = 0;
    const cJSON *ej = NULL;
    cJSON_ArrayForEach(ej, entities) {
        const cJSON *nj = cJSON_GetObjectItem(ej, "name");
        if (!nj || !cJSON_IsString(nj)) continue;

        /* Prefer stable_id lookup so renames, duplicated names, and
         * future multi-scene merges stay correct. Fall back to name for
         * legacy snapshots that predate the id. */
        ecs_entity_t e = 0;
        const cJSON *sidj = cJSON_GetObjectItem(ej, "stable_id");
        if (sidj && cJSON_IsString(sidj)) {
            SafiStableId id;
            if (safi_stable_id_from_string(sidj->valuestring, &id)) {
                e = safi_scene_find_entity_by_stable_id(world, id);
            }
        }
        if (!e) e = safi_scene_find_entity_by_name(world, nj->valuestring);
        if (!e) {
            SAFI_LOG_WARN("scene: snapshot entity '%s' not present in world; skipped",
                          nj->valuestring);
            missing++;
            continue;
        }
        deserialize_entity_components(world, e, ej);
        restored++;
    }

    SAFI_LOG_INFO("scene: restored %d entities from snapshot (%d missing)",
                  restored, missing);
    return true;
}
