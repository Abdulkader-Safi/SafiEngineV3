/**
 * safi/editor/editor_shortcuts.h — engine-owned editor keybinds.
 *
 * Interim keyboard shortcuts that stand in for the M1 toolbar UI:
 *   F1  — toggle Edit ↔ Play
 *   F6  — snapshot the world into an in-memory cJSON blob
 *   F7  — restore that blob onto the live entities (ids stay stable)
 *
 * Installed automatically by `safi_app_init` when `enable_debug_ui` is set.
 * Input is skipped while a MicroUI widget holds focus (e.g. the user is
 * typing into an Inspector field), so F-keys never fight text entry.
 *
 * Scene-file shortcuts (F5 save / F9 load) remain in user/demo code because
 * they need app-specific handle refresh after load.
 */
#ifndef SAFI_EDITOR_EDITOR_SHORTCUTS_H
#define SAFI_EDITOR_EDITOR_SHORTCUTS_H

#include <flecs.h>

void safi_editor_shortcuts_install(ecs_world_t *world);

#endif /* SAFI_EDITOR_EDITOR_SHORTCUTS_H */
