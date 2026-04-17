/**
 * safi/editor/editor_state.h — editor mode singleton.
 *
 * Source of truth for whether the app is in Edit, Play, or Paused mode.
 * Exposed as an ECS singleton so systems and panels can read it via
 * `ecs_singleton_get(world, SafiEditorState)`.
 *
 * The engine uses `mode` to gate the fixed-update and game pipelines: in
 * Edit and Paused, neither pipeline runs, so physics and gameplay systems
 * freeze while the renderer, input pump, and debug UI keep ticking.
 *
 * `selected_entity` is the Inspector's current selection. Panels write it;
 * gizmos and other editor tooling read it. 0 means "nothing selected".
 */
#ifndef SAFI_EDITOR_EDITOR_STATE_H
#define SAFI_EDITOR_EDITOR_STATE_H

#include <flecs.h>

typedef enum SafiEditorMode {
    SAFI_EDITOR_MODE_EDIT   = 0,  /* default; gameplay paused, world editable */
    SAFI_EDITOR_MODE_PLAY   = 1,  /* fixed-update + game pipelines run        */
    SAFI_EDITOR_MODE_PAUSED = 2,  /* entered Play, temporarily halted         */
} SafiEditorMode;

typedef struct SafiEditorState {
    SafiEditorMode mode;
    ecs_entity_t   selected_entity;  /* 0 = none */
} SafiEditorState;

/* Convenience accessors. Thin wrappers over ecs_singleton_get/set that
 * panels and user code can call without spelling out the singleton
 * idiom. Safe against a NULL/uninstalled singleton: getters return a
 * sensible default, setters no-op if the singleton hasn't been created. */
SafiEditorMode safi_editor_get_mode(const ecs_world_t *world);
void           safi_editor_set_mode(ecs_world_t *world, SafiEditorMode mode);
ecs_entity_t   safi_editor_get_selected(const ecs_world_t *world);
void           safi_editor_set_selected(ecs_world_t *world, ecs_entity_t e);

#endif /* SAFI_EDITOR_EDITOR_STATE_H */
