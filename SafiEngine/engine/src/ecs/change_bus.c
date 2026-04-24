#include "safi/ecs/change_bus.h"
#include "safi/ecs/component_registry.h"
#include "safi/core/log.h"

#include <string.h>

#define MAX_SUBSCRIBERS 8

typedef struct {
    SafiChangeFn cb;
    void        *ctx;
} Subscriber;

static struct {
    Subscriber subs[MAX_SUBSCRIBERS];
    int        sub_count;
    uint64_t   frame;
    uint64_t   group_id;    /* 0 when no group active */
    uint64_t   next_group;  /* monotonic id counter */
} G = { .next_group = 1 };

bool safi_change_bus_subscribe(SafiChangeFn cb, void *ctx) {
    if (!cb) return false;
    if (G.sub_count >= MAX_SUBSCRIBERS) {
        SAFI_LOG_ERROR("change_bus: subscriber table full (%d)", MAX_SUBSCRIBERS);
        return false;
    }
    G.subs[G.sub_count].cb  = cb;
    G.subs[G.sub_count].ctx = ctx;
    G.sub_count++;
    return true;
}

void safi_change_bus_unsubscribe(SafiChangeFn cb, void *ctx) {
    for (int i = 0; i < G.sub_count; i++) {
        if (G.subs[i].cb == cb && G.subs[i].ctx == ctx) {
            G.subs[i] = G.subs[G.sub_count - 1];
            G.sub_count--;
            return;
        }
    }
}

void safi_change_bus_advance_frame(uint64_t frame) { G.frame = frame; }

void safi_change_bus_begin_group(void) {
    if (G.group_id == 0) G.group_id = G.next_group++;
}
void safi_change_bus_end_group(void) { G.group_id = 0; }

/* One observer per serializable component — fires when ecs_set writes. */
static void on_component_set(ecs_iter_t *it) {
    if (G.sub_count == 0) return;
    for (int i = 0; i < it->count; i++) {
        SafiChange change = {
            .entity    = it->entities[i],
            .component = ecs_field_id(it, 0),
            .frame     = G.frame,
            .group_id  = G.group_id,
        };
        for (int s = 0; s < G.sub_count; s++) {
            G.subs[s].cb(&change, G.subs[s].ctx);
        }
    }
}

void safi_change_bus_install(ecs_world_t *world) {
    if (!world) return;
    int count = safi_component_registry_count();
    int installed = 0;
    for (int i = 0; i < count; i++) {
        const SafiComponentInfo *ci = safi_component_registry_get(i);
        if (!ci || !ci->serializable || !ci->id) continue;
        ecs_observer(world, {
            .query.terms = {{ .id = ci->id }},
            .events      = { EcsOnSet },
            .callback    = on_component_set,
        });
        installed++;
    }
    SAFI_LOG_INFO("change_bus: observing %d component types", installed);
}
