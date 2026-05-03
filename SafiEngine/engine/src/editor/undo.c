#include "safi/editor/undo.h"
#include "safi/ecs/change_bus.h"
#include "safi/ecs/components.h"
#include "safi/scene/scene.h"
#include "safi/core/log.h"

#include <cJSON.h>
#include <stdint.h>
#include <string.h>

/* ---- Configuration ------------------------------------------------------ */

#define UNDO_RING  64
#define CACHE_CAP 512   /* must be power of 2 for the cheap mask below */

#if (CACHE_CAP & (CACHE_CAP - 1)) != 0
#error "CACHE_CAP must be a power of two"
#endif

/* ---- Step + cache types ------------------------------------------------- */

typedef struct {
    uint64_t     group_id;
    uint64_t     frame;
    ecs_entity_t entity;
    cJSON       *before;     /* state before this step's write           */
    cJSON       *after;      /* state at end of group / after this write */
} UndoStep;

typedef struct {
    ecs_entity_t key;     /* 0 = empty */
    cJSON       *snap;    /* latest known state for this entity         */
} CacheSlot;

/* ---- File-static state -------------------------------------------------- */

static struct {
    ecs_world_t *world;
    bool         installed;
    bool         applying;        /* true while restoring; suppresses bus events */

    /* Two rings, each acting as a LIFO stack. We store entries linearly
     * up to UNDO_RING and overwrite from the bottom on overflow (FIFO
     * eviction of the oldest steps). */
    UndoStep undo[UNDO_RING];
    int      undo_count;          /* 0..UNDO_RING                       */

    UndoStep redo[UNDO_RING];
    int      redo_count;

    CacheSlot cache[CACHE_CAP];
} S;

/* ---- Cache (open-addressed, linear probe) ------------------------------- */

static int cache_index(ecs_entity_t e) {
    /* Splitmix-style mix to spread sequential entity ids. */
    uint64_t x = (uint64_t)e * 11400714819323198485ull;
    return (int)(x & (uint64_t)(CACHE_CAP - 1));
}

/* Find slot for `e`; returns either an exact match or the first empty
 * slot in the probe chain. NULL if the table is full (shouldn't happen). */
static CacheSlot *cache_probe(ecs_entity_t e) {
    int h = cache_index(e);
    for (int i = 0; i < CACHE_CAP; i++) {
        int idx = (h + i) & (CACHE_CAP - 1);
        CacheSlot *s = &S.cache[idx];
        if (s->key == 0 || s->key == e) return s;
    }
    return NULL;
}

static cJSON *cache_get(ecs_entity_t e) {
    CacheSlot *s = cache_probe(e);
    return (s && s->key == e) ? s->snap : NULL;
}

/* Store `snap` for entity `e`, taking ownership. Frees any previous
 * snapshot in the slot (including ones for dead entities that happened
 * to land in the same slot). */
static void cache_put(ecs_entity_t e, cJSON *snap) {
    CacheSlot *s = cache_probe(e);
    if (!s) { cJSON_Delete(snap); return; }
    if (s->snap) cJSON_Delete(s->snap);
    s->key  = e;
    s->snap = snap;
}

static void cache_clear(void) {
    for (int i = 0; i < CACHE_CAP; i++) {
        if (S.cache[i].snap) cJSON_Delete(S.cache[i].snap);
        S.cache[i].key  = 0;
        S.cache[i].snap = NULL;
    }
}

/* ---- Helpers ------------------------------------------------------------ */

static cJSON *snap_one(ecs_entity_t e) {
    return safi_scene_snapshot_entities(S.world, &e, 1);
}

static void step_free(UndoStep *s) {
    if (!s) return;
    if (s->before) cJSON_Delete(s->before);
    if (s->after)  cJSON_Delete(s->after);
    s->before = s->after = NULL;
}

/* Push a step onto the given ring with FIFO eviction of the bottom slot
 * once the ring is full. */
static UndoStep *ring_push(UndoStep *ring, int *count) {
    if (*count >= UNDO_RING) {
        /* Evict the oldest (index 0), shift everyone down. The ring is
         * small (64 entries) so the shift cost is negligible compared to
         * a cJSON snapshot. */
        step_free(&ring[0]);
        memmove(&ring[0], &ring[1], (UNDO_RING - 1) * sizeof(UndoStep));
        memset(&ring[UNDO_RING - 1], 0, sizeof(UndoStep));
        *count = UNDO_RING - 1;
    }
    UndoStep *slot = &ring[*count];
    (*count)++;
    return slot;
}

static void ring_clear(UndoStep *ring, int *count) {
    for (int i = 0; i < *count; i++) step_free(&ring[i]);
    memset(ring, 0, sizeof(UndoStep) * UNDO_RING);
    *count = 0;
}

/* ---- Change-bus callback ------------------------------------------------ */

static void on_change(const SafiChange *c, void *ctx) {
    (void)ctx;
    if (S.applying || !S.world) return;
    if (!c->entity || !ecs_is_alive(S.world, c->entity)) return;

    /* Only track named entities — `safi_scene_restore_snapshot` looks up
     * by stable_id / SafiName, so anonymous entities can't be restored. */
    if (!ecs_has(S.world, c->entity, SafiName)) return;

    cJSON *before_cached = cache_get(c->entity);
    if (!before_cached) {
        /* First time we see this entity. We can't recover its pre-write
         * state from the bus alone; seed the cache with the post-write
         * state so future edits have something to undo against. */
        cJSON *snap = snap_one(c->entity);
        cache_put(c->entity, snap);
        return;
    }

    cJSON *after_snap = snap_one(c->entity);
    if (!after_snap) return;

    /* Coalesce with the most-recent step when both share the same
     * non-zero group id and same entity (e.g. a gizmo drag). Replaces
     * the step's `after` with the freshest snapshot; the original
     * `before` stays put. */
    if (S.undo_count > 0 && c->group_id != 0) {
        UndoStep *top = &S.undo[S.undo_count - 1];
        if (top->group_id == c->group_id && top->entity == c->entity) {
            if (top->after) cJSON_Delete(top->after);
            top->after = after_snap;
            top->frame = c->frame;
            cache_put(c->entity, cJSON_Duplicate(after_snap, 1));
            ring_clear(S.redo, &S.redo_count);
            return;
        }
    }

    UndoStep *step = ring_push(S.undo, &S.undo_count);
    step->group_id = c->group_id;
    step->frame    = c->frame;
    step->entity   = c->entity;
    step->before   = cJSON_Duplicate(before_cached, 1);
    step->after    = after_snap;

    cache_put(c->entity, cJSON_Duplicate(after_snap, 1));
    ring_clear(S.redo, &S.redo_count);
}

/* ---- Observer: seed the cache when a SafiName appears ------------------ */

static void on_named_added(ecs_iter_t *it) {
    for (int i = 0; i < it->count; i++) {
        ecs_entity_t e = it->entities[i];
        if (!cache_get(e)) {
            cJSON *snap = snap_one(e);
            if (snap) cache_put(e, snap);
        }
    }
}

/* ---- Public API --------------------------------------------------------- */

void safi_undo_install(ecs_world_t *world) {
    if (!world || S.installed) return;
    S.world = world;

    /* Seed cache from every currently named entity. */
    ecs_query_t *q = ecs_query(world, {
        .terms = {{ .id = ecs_id(SafiName) }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        for (int i = 0; i < it.count; i++) {
            ecs_entity_t e = it.entities[i];
            cJSON *snap = snap_one(e);
            if (snap) cache_put(e, snap);
        }
    }
    ecs_query_fini(q);

    /* Track entities that gain a SafiName *after* install. */
    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(SafiName) }},
        .events      = { EcsOnAdd },
        .callback    = on_named_added,
    });

    safi_change_bus_subscribe(on_change, NULL);
    S.installed = true;
    SAFI_LOG_INFO("undo: installed (ring=%d, cache=%d)", UNDO_RING, CACHE_CAP);
}

void safi_undo_reset(void) {
    ring_clear(S.undo, &S.undo_count);
    ring_clear(S.redo, &S.redo_count);
    cache_clear();
}

int safi_undo_depth(void)      { return S.undo_count; }
int safi_undo_redo_depth(void) { return S.redo_count; }

void safi_undo_perform(ecs_world_t *world) {
    if (S.undo_count == 0 || !world) return;

    /* Pop the top step. */
    UndoStep step = S.undo[S.undo_count - 1];
    memset(&S.undo[S.undo_count - 1], 0, sizeof(UndoStep));
    S.undo_count--;

    S.applying = true;
    safi_scene_restore_snapshot(world, step.before);
    S.applying = false;

    /* The world is now in `before` state — refresh the cache. */
    cache_put(step.entity, cJSON_Duplicate(step.before, 1));

    /* Push onto redo (taking ownership of the cJSON pointers). */
    UndoStep *r = ring_push(S.redo, &S.redo_count);
    *r = step;
}

void safi_undo_redo(ecs_world_t *world) {
    if (S.redo_count == 0 || !world) return;

    UndoStep step = S.redo[S.redo_count - 1];
    memset(&S.redo[S.redo_count - 1], 0, sizeof(UndoStep));
    S.redo_count--;

    S.applying = true;
    safi_scene_restore_snapshot(world, step.after);
    S.applying = false;

    cache_put(step.entity, cJSON_Duplicate(step.after, 1));

    UndoStep *u = ring_push(S.undo, &S.undo_count);
    *u = step;
}
