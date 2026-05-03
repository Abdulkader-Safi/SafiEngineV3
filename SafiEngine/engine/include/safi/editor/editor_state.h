/**
 * safi/editor/editor_state.h — editor mode singleton + selection helpers.
 *
 * Source of truth for whether the app is in Edit, Play, or Paused mode.
 * Exposed as an ECS singleton so systems and panels can read it via
 * `ecs_singleton_get(world, SafiEditorState)`.
 *
 * The engine uses `mode` to gate the fixed-update and game pipelines: in
 * Edit and Paused, neither pipeline runs, so physics and gameplay systems
 * freeze while the renderer, input pump, and debug UI keep ticking.
 *
 * Selection is stored as a `SafiSelected` tag on each selected entity
 * (see safi/ecs/components.h), so the active selection can be any number
 * of entities. The helpers below mutate that tag set; the legacy
 * `safi_editor_get_selected` / `_set_selected` accessors keep working in
 * single-select mode by returning the first tagged entity (get) or
 * clearing-and-tagging exactly one (set).
 */
#ifndef SAFI_EDITOR_EDITOR_STATE_H
#define SAFI_EDITOR_EDITOR_STATE_H

#include <stdbool.h>
#include <flecs.h>

typedef enum SafiEditorMode {
    SAFI_EDITOR_MODE_EDIT   = 0,  /* default; gameplay paused, world editable */
    SAFI_EDITOR_MODE_PLAY   = 1,  /* fixed-update + game pipelines run        */
    SAFI_EDITOR_MODE_PAUSED = 2,  /* entered Play, temporarily halted         */
} SafiEditorMode;

/* Which manipulator the user is currently using in the viewport. Drives
 * both the toolbar's highlight state and the gizmo system's per-frame
 * draw + hit-test. Q / W / E / R cycle through these. */
typedef enum SafiEditorTool {
    SAFI_EDITOR_TOOL_SELECT    = 0,  /* default: click to pick entities   */
    SAFI_EDITOR_TOOL_TRANSLATE = 1,  /* three axis arrows                 */
    SAFI_EDITOR_TOOL_ROTATE    = 2,  /* three orthogonal rings            */
    SAFI_EDITOR_TOOL_SCALE     = 3,  /* three axes ending in cubes        */
} SafiEditorTool;

typedef struct SafiEditorState {
    SafiEditorMode mode;
    SafiEditorTool selected_tool;
} SafiEditorState;

/* ---- Mode / tool -------------------------------------------------------- */

SafiEditorMode safi_editor_get_mode(const ecs_world_t *world);
void           safi_editor_set_mode(ecs_world_t *world, SafiEditorMode mode);
SafiEditorTool safi_editor_get_tool(const ecs_world_t *world);
void           safi_editor_set_tool(ecs_world_t *world, SafiEditorTool tool);

/* ---- Selection (multi) -------------------------------------------------- *
 * All helpers no-op safely on dead/zero entities. Callers that want
 * single-select semantics can stick to safi_editor_set_selected (clears
 * then adds) and safi_editor_get_selected (returns the first tagged). */

void         safi_editor_add_selection    (ecs_world_t *world, ecs_entity_t e);
void         safi_editor_remove_selection (ecs_world_t *world, ecs_entity_t e);
void         safi_editor_clear_selection  (ecs_world_t *world);
bool         safi_editor_is_selected      (const ecs_world_t *world, ecs_entity_t e);
int          safi_editor_selection_count  (const ecs_world_t *world);

/* Fills `out` with up to `cap` selected entity ids; returns count written.
 * Order is whatever flecs's query iterator gives back; sort if you need
 * deterministic ordering. */
int          safi_editor_selection        (const ecs_world_t *world,
                                            ecs_entity_t *out, int cap);

/* Legacy single-select accessor — returns the first tagged entity, or 0
 * when nothing is selected. Existing inspector / gizmo code that only
 * edits one entity at a time can keep using this unchanged. */
ecs_entity_t safi_editor_get_selected     (const ecs_world_t *world);

/* Replaces the selection with exactly `e` (clears every prior tag, then
 * tags `e`). Pass 0 to clear without selecting anything new. */
void         safi_editor_set_selected     (ecs_world_t *world, ecs_entity_t e);

#endif /* SAFI_EDITOR_EDITOR_STATE_H */
