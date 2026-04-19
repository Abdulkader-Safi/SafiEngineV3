/**
 * safi/editor/editor_gizmo.h — translate / rotate / scale gizmo handles.
 *
 * Draws manipulator geometry for the currently selected entity in Edit
 * mode, and (eventually) processes hit-test + drag to mutate that
 * entity's `SafiTransform`. Installed automatically by `safi_app_init`
 * when the debug UI is on.
 *
 * The system runs on `EcsOnUpdate` (variable phase, always ticks in
 * Edit), piggybacks on the existing `safi_gizmo_draw_*` primitives for
 * rendering, and reads `safi_editor_get_tool` / `safi_editor_get_selected`
 * to decide what to draw.
 */
#ifndef SAFI_EDITOR_EDITOR_GIZMO_H
#define SAFI_EDITOR_EDITOR_GIZMO_H

#include <flecs.h>

void safi_editor_gizmo_install(ecs_world_t *world);

#endif /* SAFI_EDITOR_EDITOR_GIZMO_H */
