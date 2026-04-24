#include "safi/ecs/singleton_tag.h"

#define MAX_HOLDERS 32

void safi_ecs_make_tag_unique(ecs_world_t *world,
                              ecs_id_t     tag_id,
                              ecs_entity_t holder) {
    if (!world || !tag_id) return;

    /* Collect offenders into a local buffer so we can remove them without
     * mutating the world mid-iteration. 32 is plenty — if this ever
     * overflows we'd have a much bigger design problem than a truncated
     * sweep. */
    ecs_entity_t losers[MAX_HOLDERS];
    int loser_count = 0;

    ecs_query_t *q = ecs_query(world, {
        .terms      = {{ .id = tag_id }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        for (int i = 0; i < it.count && loser_count < MAX_HOLDERS; i++) {
            if (it.entities[i] != holder) losers[loser_count++] = it.entities[i];
        }
    }
    ecs_query_fini(q);

    for (int i = 0; i < loser_count; i++) {
        ecs_remove_id(world, losers[i], tag_id);
    }
}
