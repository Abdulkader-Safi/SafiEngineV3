#include "safi/editor/editor_state.h"
#include "safi/ecs/components.h"
#include "safi/ecs/stable_id.h"

SafiEditorMode safi_editor_get_mode(const ecs_world_t *world) {
    const SafiEditorState *s = ecs_singleton_get(world, SafiEditorState);
    return s ? s->mode : SAFI_EDITOR_MODE_EDIT;
}

void safi_editor_set_mode(ecs_world_t *world, SafiEditorMode mode) {
    const SafiEditorState *s = ecs_singleton_get(world, SafiEditorState);
    if (!s) return;
    SafiEditorState next = *s;
    next.mode = mode;
    ecs_singleton_set_ptr(world, SafiEditorState, &next);
}

SafiEditorTool safi_editor_get_tool(const ecs_world_t *world) {
    const SafiEditorState *s = ecs_singleton_get(world, SafiEditorState);
    return s ? s->selected_tool : SAFI_EDITOR_TOOL_SELECT;
}

void safi_editor_set_tool(ecs_world_t *world, SafiEditorTool tool) {
    const SafiEditorState *s = ecs_singleton_get(world, SafiEditorState);
    if (!s) return;
    SafiEditorState next = *s;
    next.selected_tool = tool;
    ecs_singleton_set_ptr(world, SafiEditorState, &next);
}

/* ---- Selection ---------------------------------------------------------- */

void safi_editor_add_selection(ecs_world_t *world, ecs_entity_t e) {
    if (!e || !ecs_is_alive(world, e)) return;
    if (!ecs_has(world, e, SafiSelected)) {
        ecs_add(world, e, SafiSelected);
    }
}

void safi_editor_remove_selection(ecs_world_t *world, ecs_entity_t e) {
    if (!e || !ecs_is_alive(world, e)) return;
    if (ecs_has(world, e, SafiSelected)) {
        ecs_remove(world, e, SafiSelected);
    }
}

void safi_editor_clear_selection(ecs_world_t *world) {
    /* Two-pass: collect, then mutate. Removing inside ecs_query_next would
     * invalidate the iterator. Cap at 256 — enough for any plausible
     * editor selection; loop again if more remain.
     *
     * Note: when we hit CAP we stop iterating early, so we must
     * `ecs_iter_fini(&it)` to release flecs's per-iterator stack state. */
    enum { CAP = 256 };
    ecs_entity_t buf[CAP];
    int n;
    do {
        n = 0;
        bool capped = false;
        ecs_query_t *q = ecs_query(world, {
            .terms      = {{ .id = ecs_id(SafiSelected) }},
            .cache_kind = EcsQueryCacheNone,
        });
        ecs_iter_t it = ecs_query_iter(world, q);
        while (ecs_query_next(&it)) {
            for (int i = 0; i < it.count; i++) {
                if (n >= CAP) { capped = true; break; }
                buf[n++] = it.entities[i];
            }
            if (capped) { ecs_iter_fini(&it); break; }
        }
        ecs_query_fini(q);
        for (int i = 0; i < n; i++) {
            if (ecs_is_alive(world, buf[i])) {
                ecs_remove(world, buf[i], SafiSelected);
            }
        }
    } while (n == CAP);
}

bool safi_editor_is_selected(const ecs_world_t *world, ecs_entity_t e) {
    if (!e || !ecs_is_alive(world, e)) return false;
    return ecs_has(world, e, SafiSelected);
}

int safi_editor_selection_count(const ecs_world_t *world) {
    int n = 0;
    ecs_query_t *q = ecs_query((ecs_world_t *)world, {
        .terms      = {{ .id = ecs_id(SafiSelected) }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) n += it.count;
    ecs_query_fini(q);
    return n;
}

int safi_editor_selection(const ecs_world_t *world, ecs_entity_t *out, int cap) {
    if (!out || cap <= 0) return 0;
    int n = 0;
    bool stopped_early = false;
    ecs_query_t *q = ecs_query((ecs_world_t *)world, {
        .terms      = {{ .id = ecs_id(SafiSelected) }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        for (int i = 0; i < it.count; i++) {
            if (n >= cap) { stopped_early = true; break; }
            out[n++] = it.entities[i];
        }
        if (stopped_early) { ecs_iter_fini(&it); break; }
    }
    ecs_query_fini(q);
    return n;
}

ecs_entity_t safi_editor_get_selected(const ecs_world_t *world) {
    ecs_entity_t out = 0;
    ecs_query_t *q = ecs_query((ecs_world_t *)world, {
        .terms      = {{ .id = ecs_id(SafiSelected) }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        if (it.count > 0) {
            out = it.entities[0];
            ecs_iter_fini(&it);
            break;
        }
    }
    ecs_query_fini(q);
    return out;
}

void safi_editor_set_selected(ecs_world_t *world, ecs_entity_t e) {
    safi_editor_clear_selection(world);
    safi_editor_add_selection(world, e);
}

/* ---- Persistence across reloads ---------------------------------------- */

int safi_editor_capture_selection_ids(const ecs_world_t *world,
                                      SafiStableId *out, int cap) {
    if (!out || cap <= 0) return 0;

    /* Reuse the live-selection collector. 256 covers any plausible
     * hand-driven multi-select; we silently drop the tail past `cap`. */
    enum { TMP = 256 };
    ecs_entity_t buf[TMP];
    int n_ent = safi_editor_selection(world, buf, TMP);

    int n_ids = 0;
    for (int i = 0; i < n_ent && n_ids < cap; i++) {
        const SafiStableId *sid = ecs_get(world, buf[i], SafiStableId);
        if (sid && !safi_stable_id_is_zero(*sid)) {
            out[n_ids++] = *sid;
        }
    }
    return n_ids;
}

void safi_editor_restore_selection_ids(ecs_world_t *world,
                                       const SafiStableId *ids, int count) {
    safi_editor_clear_selection(world);
    if (!ids || count <= 0) return;

    for (int i = 0; i < count; i++) {
        ecs_entity_t e = safi_scene_find_entity_by_stable_id(world, ids[i]);
        if (e) safi_editor_add_selection(world, e);
    }
}
