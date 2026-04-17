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
        SafiName *names = ecs_field(&it, SafiName, 0);
        for (int i = 0; i < it.count; i++) {
            ecs_entity_t e = it.entities[i];
            if (!names[i].value) continue;

            cJSON *ej = cJSON_CreateObject();
            cJSON_AddStringToObject(ej, "name", names[i].value);

            /* Parent reference. */
            ecs_entity_t parent = ecs_get_target(world, e, EcsChildOf, 0);
            if (parent) {
                const SafiName *pn = ecs_get(world, parent, SafiName);
                if (pn && pn->value)
                    cJSON_AddStringToObject(ej, "parent", pn->value);
            }

            /* Serialize each registered component. */
            cJSON *comps = cJSON_CreateObject();
            int reg_count = safi_component_registry_count();
            for (int c = 0; c < reg_count; c++) {
                const SafiComponentInfo *ci = safi_component_registry_get(c);
                if (!ci->serializable || !ci->serialize) continue;
                if (!ci->id) continue;  /* not yet registered (e.g. physics) */
                /* Skip SafiName — already stored as top-level "name". */
                if (ci->id == ecs_id(SafiName)) continue;
                if (!ecs_has_id(world, e, ci->id)) continue;

                cJSON *cj = ci->serialize(world, e, ci->id);
                if (cj) cJSON_AddItemToObject(comps, ci->name, cj);
            }
            cJSON_AddItemToObject(ej, "components", comps);
            cJSON_AddItemToArray(entities, ej);
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
     * invalidates the iterator. */
    ecs_entity_t buf[MAX_ENTITIES];
    int count = 0;

    ecs_query_t *q = ecs_query(world, {
        .terms = {{ .id = ecs_id(SafiName) }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        for (int i = 0; i < it.count && count < MAX_ENTITIES; i++)
            buf[count++] = it.entities[i];
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

        map[i].name   = pooled;
        map[i].entity = e;
    }

    /* --- Pass 2: deserialize components --- */
    for (int i = 0; i < entity_count; i++) {
        cJSON *ej = cJSON_GetArrayItem(entities, i);
        cJSON *comps = cJSON_GetObjectItem(ej, "components");
        if (!comps) continue;

        cJSON *cj = NULL;
        cJSON_ArrayForEach(cj, comps) {
            const char *comp_name = cj->string;
            const SafiComponentInfo *ci =
                safi_component_registry_find_by_name(comp_name);
            if (!ci || !ci->deserialize) {
                SAFI_LOG_WARN("scene: unknown component '%s' on entity '%s'",
                              comp_name, map[i].name);
                continue;
            }
            ci->deserialize(world, map[i].entity, cj);
        }

        /* Ensure GlobalTransform is present if Transform was deserialized,
         * so the propagation system writes the world matrix. */
        if (ecs_has(world, map[i].entity, SafiTransform) &&
            !ecs_has(world, map[i].entity, SafiGlobalTransform)) {
            ecs_set(world, map[i].entity, SafiGlobalTransform, {0});
        }
    }

    /* --- Pass 3: wire parent/child --- */
    for (int i = 0; i < entity_count; i++) {
        cJSON *ej = cJSON_GetArrayItem(entities, i);
        cJSON *pj = cJSON_GetObjectItem(ej, "parent");
        if (!pj || !cJSON_IsString(pj)) continue;

        const char *parent_name = pj->valuestring;
        for (int j = 0; j < entity_count; j++) {
            if (strcmp(map[j].name, parent_name) == 0) {
                ecs_add_pair(world, map[i].entity, EcsChildOf, map[j].entity);
                break;
            }
        }
    }

    free(map);
    cJSON_Delete(root);

    SAFI_LOG_INFO("scene: loaded %d entities from '%s'", entity_count, path);
    return true;
}
