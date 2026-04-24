#include "safi/ecs/stable_id.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

/* splitmix64 — cheap well-distributed PRNG. Seeded once per process from
 * high-resolution counters and the address of the generator state itself
 * so different processes on the same machine produce independent streams. */
static uint64_t g_rng_state;
static bool     g_rng_seeded = false;

static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void seed_rng_once(void) {
    if (g_rng_seeded) return;
    uint64_t a = SDL_GetPerformanceCounter();
    uint64_t b = (uint64_t)(uintptr_t)&g_rng_state;
    g_rng_state = a ^ (b * 0x9E3779B97F4A7C15ULL);
    if (g_rng_state == 0) g_rng_state = 0xDEADBEEFCAFEBABEULL;
    g_rng_seeded = true;
}

SafiStableId safi_stable_id_new(void) {
    seed_rng_once();
    SafiStableId id;
    id.hi = splitmix64(&g_rng_state);
    id.lo = splitmix64(&g_rng_state);
    /* {0,0} is reserved as the "invalid" sentinel — nudge either half
     * in the astronomically unlikely event the RNG produces it. */
    if (id.hi == 0 && id.lo == 0) id.lo = 1;
    return id;
}

static const char HEX[] = "0123456789abcdef";

static void u64_to_hex(uint64_t v, char out[16]) {
    for (int i = 15; i >= 0; i--) {
        out[i] = HEX[v & 0xFu];
        v >>= 4;
    }
}
static bool hex_to_u64(const char *s, uint64_t *out) {
    uint64_t v = 0;
    for (int i = 0; i < 16; i++) {
        char c = s[i];
        uint64_t d;
        if      (c >= '0' && c <= '9') d = (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint64_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint64_t)(c - 'A' + 10);
        else return false;
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}

void safi_stable_id_to_string(SafiStableId id, char out[33]) {
    u64_to_hex(id.hi, out);
    u64_to_hex(id.lo, out + 16);
    out[32] = '\0';
}

bool safi_stable_id_from_string(const char *str, SafiStableId *out) {
    if (!str || !out) return false;
    if (strlen(str) != 32) return false;
    SafiStableId id;
    if (!hex_to_u64(str, &id.hi))        return false;
    if (!hex_to_u64(str + 16, &id.lo))   return false;
    *out = id;
    return true;
}

/* OnAdd observer: grants a stable id to every newly-named entity that
 * doesn't already carry one. Engine infrastructure (SafiEngineOwned) is
 * skipped — its entities are not serialized and don't need identity. */
static void on_name_added(ecs_iter_t *it) {
    for (int i = 0; i < it->count; i++) {
        ecs_entity_t e = it->entities[i];
        if (ecs_has(it->world, e, SafiEngineOwned)) continue;
        if (ecs_has(it->world, e, SafiStableId))    continue;
        SafiStableId id = safi_stable_id_new();
        ecs_set_ptr(it->world, e, SafiStableId, &id);
    }
}

void safi_stable_id_install(ecs_world_t *world) {
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(SafiName) }},
        .events      = { EcsOnAdd },
        .callback    = on_name_added,
    });
}

ecs_entity_t safi_scene_find_entity_by_stable_id(ecs_world_t *world,
                                                  SafiStableId id) {
    if (!world || safi_stable_id_is_zero(id)) return 0;

    ecs_query_t *q = ecs_query(world, {
        .terms      = {{ .id = ecs_id(SafiStableId) }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_entity_t found = 0;
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        SafiStableId *ids = ecs_field(&it, SafiStableId, 0);
        for (int i = 0; i < it.count; i++) {
            if (safi_stable_id_equal(ids[i], id)) {
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
