#include "safi/editor/editor_shortcuts.h"
#include "safi/editor/editor_state.h"
#include "safi/ecs/components.h"
#include "safi/input/input.h"
#include "safi/scene/scene.h"
#include "safi/ui/debug_ui.h"
#include "safi/core/log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>
#include <cJSON.h>

/* Scratchpad blob for F6 / F7. Single-slot by design — these are keybinds,
 * not a full undo history. Freed on the next F6 or at process exit. */
static cJSON *g_snapshot_scratch = NULL;

static void editor_shortcuts_system(ecs_iter_t *it) {
    /* Never steal keystrokes from an Inspector text field. */
    if (safi_debug_ui_wants_input()) return;

    const SafiInput *in = ecs_singleton_get(it->world, SafiInput);
    if (!in) return;

    if (in->keys_pressed[SDL_SCANCODE_F1]) {
        SafiEditorMode m    = safi_editor_get_mode(it->world);
        SafiEditorMode next = (m == SAFI_EDITOR_MODE_PLAY)
                                ? SAFI_EDITOR_MODE_EDIT
                                : SAFI_EDITOR_MODE_PLAY;
        safi_editor_set_mode(it->world, next);
        SAFI_LOG_INFO("editor: mode → %s",
                      next == SAFI_EDITOR_MODE_PLAY ? "Play" : "Edit");
    }

    if (in->keys_pressed[SDL_SCANCODE_F6]) {
        if (g_snapshot_scratch) cJSON_Delete(g_snapshot_scratch);
        g_snapshot_scratch = safi_scene_snapshot_all(it->world);
        SAFI_LOG_INFO("editor: snapshot captured");
    }

    if (in->keys_pressed[SDL_SCANCODE_F7] && g_snapshot_scratch) {
        safi_scene_restore_snapshot(it->world, g_snapshot_scratch);
    }

    /* Q / W / E / R — tool switch. Gated to Edit mode because in Play W / S
     * may drive user gameplay. Also skipped while the right mouse button is
     * held: RMB+WASD is the fly-cam's flight gesture, so those keys belong
     * to the camera and must not also flip the tool picker. */
    bool rmb = in->mouse_buttons[SDL_BUTTON_RIGHT];
    if (!rmb && safi_editor_get_mode(it->world) == SAFI_EDITOR_MODE_EDIT) {
        if (in->keys_pressed[SDL_SCANCODE_Q])
            safi_editor_set_tool(it->world, SAFI_EDITOR_TOOL_SELECT);
        if (in->keys_pressed[SDL_SCANCODE_W])
            safi_editor_set_tool(it->world, SAFI_EDITOR_TOOL_TRANSLATE);
        if (in->keys_pressed[SDL_SCANCODE_E])
            safi_editor_set_tool(it->world, SAFI_EDITOR_TOOL_ROTATE);
        if (in->keys_pressed[SDL_SCANCODE_R])
            safi_editor_set_tool(it->world, SAFI_EDITOR_TOOL_SCALE);
    }
}

void safi_editor_shortcuts_install(ecs_world_t *world) {
    if (!world) return;
    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "safi_editor_shortcuts",
            .add  = ecs_ids(ecs_dependson(EcsOnUpdate)),
        }),
        .callback = editor_shortcuts_system,
    });
}
