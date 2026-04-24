#include "safi/ecs/component_registry.h"
#include "safi/core/log.h"

#include <string.h>

#define MAX_COMPONENTS 64

static SafiComponentInfo g_registry[MAX_COMPONENTS];
static int               g_count = 0;
static bool              g_initialized = false;

void safi_component_registry_init(void) {
    if (g_initialized) return;
    memset(g_registry, 0, sizeof(g_registry));
    g_count = 0;
    g_initialized = true;
}

void safi_component_registry_register(const SafiComponentInfo *info) {
    if (!info || !info->name) return;
    if (g_count >= MAX_COMPONENTS) {
        SAFI_LOG_ERROR("component_registry: full (%d max)", MAX_COMPONENTS);
        return;
    }
    g_registry[g_count] = *info;
    g_count++;
}

int safi_component_registry_count(void) {
    return g_count;
}

const SafiComponentInfo *safi_component_registry_get(int index) {
    if (index < 0 || index >= g_count) return NULL;
    return &g_registry[index];
}

const SafiComponentInfo *safi_component_registry_find(ecs_id_t id) {
    for (int i = 0; i < g_count; i++) {
        if (g_registry[i].id == id) return &g_registry[i];
    }
    return NULL;
}

const SafiComponentInfo *safi_component_registry_find_by_name(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_registry[i].name, name) == 0) return &g_registry[i];
    }
    return NULL;
}

bool safi_component_registry_construct(ecs_world_t *world,
                                        ecs_entity_t entity,
                                        const char *name) {
    const SafiComponentInfo *ci = safi_component_registry_find_by_name(name);
    if (!ci || !ci->id) return false;
    if (ci->default_init) {
        ci->default_init(world, entity, ci->id);
    } else {
        ecs_add_id(world, entity, ci->id);
    }
    return true;
}
