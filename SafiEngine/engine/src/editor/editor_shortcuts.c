#include "safi/editor/editor_shortcuts.h"
#include "safi/editor/editor_state.h"
#include "safi/editor/undo.h"
#include "safi/ecs/components.h"
#include "safi/input/input.h"
#include "safi/scene/scene.h"
#include "safi/ui/debug_ui.h"
#include "safi/core/log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <cJSON.h>
#include <string.h>

/* ---- Chord table -------------------------------------------------------- */

#define MAX_CHORDS 32

static SafiEditorChord s_chords[MAX_CHORDS];
static int             s_chord_count;

bool safi_editor_shortcuts_register(const SafiEditorChord *chord) {
    if (!chord || !chord->callback) return false;
    if (s_chord_count >= MAX_CHORDS) {
        SAFI_LOG_ERROR("editor_shortcuts: table full (%d)", MAX_CHORDS);
        return false;
    }
    /* Reject exact duplicates so a re-install doesn't double-register. */
    for (int i = 0; i < s_chord_count; i++) {
        if (s_chords[i].key == chord->key &&
            s_chords[i].required_mods == chord->required_mods) {
            return false;
        }
    }
    s_chords[s_chord_count++] = *chord;
    return true;
}

/* Compare each modifier family as a single "is held / not held" bit so
 * SDL_KMOD_CTRL (= LCTRL | RCTRL) matches whichever physical key the user
 * pressed. Lock keys (NUM / CAPS / SCROLL) are ignored. */
static bool chord_mods_match(uint16_t input, uint16_t required) {
    static const uint16_t fams[] = {
        SDL_KMOD_CTRL,  SDL_KMOD_SHIFT,
        SDL_KMOD_GUI,   SDL_KMOD_ALT,
    };
    for (int i = 0; i < (int)(sizeof(fams) / sizeof(fams[0])); i++) {
        bool input_has = (input    & fams[i]) != 0;
        bool req_has   = (required & fams[i]) != 0;
        if (input_has != req_has) return false;
    }
    return true;
}

/* ---- Stock callbacks ---------------------------------------------------- */

/* Scratchpad blob for F6 / F7. Single-slot by design — these are keybinds,
 * not a full undo history. Freed on the next F6 or at process exit. */
static cJSON *g_snapshot_scratch = NULL;

static void cb_toggle_play(ecs_world_t *w) {
    SafiEditorMode m    = safi_editor_get_mode(w);
    SafiEditorMode next = (m == SAFI_EDITOR_MODE_PLAY)
                            ? SAFI_EDITOR_MODE_EDIT
                            : SAFI_EDITOR_MODE_PLAY;
    safi_editor_set_mode(w, next);
    SAFI_LOG_INFO("editor: mode → %s",
                  next == SAFI_EDITOR_MODE_PLAY ? "Play" : "Edit");
}

static void cb_snapshot(ecs_world_t *w) {
    if (g_snapshot_scratch) cJSON_Delete(g_snapshot_scratch);
    g_snapshot_scratch = safi_scene_snapshot_all(w);
    SAFI_LOG_INFO("editor: snapshot captured");
}

static void cb_restore(ecs_world_t *w) {
    if (g_snapshot_scratch) safi_scene_restore_snapshot(w, g_snapshot_scratch);
}

static void cb_undo(ecs_world_t *w) { safi_undo_perform(w); }
static void cb_redo(ecs_world_t *w) { safi_undo_redo(w); }

/* ---- Tick --------------------------------------------------------------- */

static void editor_shortcuts_system(ecs_iter_t *it) {
    /* Never steal keystrokes from an Inspector text field. */
    if (safi_debug_ui_wants_input()) return;

    const SafiInput *in = ecs_singleton_get(it->world, SafiInput);
    if (!in) return;

    for (int i = 0; i < s_chord_count; i++) {
        const SafiEditorChord *c = &s_chords[i];
        if (!in->keys_pressed[c->key]) continue;
        if (!chord_mods_match(in->modifiers, c->required_mods)) continue;
        c->callback(it->world);
    }

    /* Q / W / E / R — tool switch. Kept inline because they share two
     * special guards (Edit-only + suppressed while RMB is held for the
     * fly-cam) that don't fit the generic chord predicate. */
    bool rmb = in->mouse_buttons[SDL_BUTTON_RIGHT];
    if (!rmb && safi_editor_get_mode(it->world) == SAFI_EDITOR_MODE_EDIT &&
        chord_mods_match(in->modifiers, 0)) {
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

/* ---- Install ------------------------------------------------------------ */

void safi_editor_shortcuts_install(ecs_world_t *world) {
    if (!world) return;

    /* Built-in chords. Idempotent: register rejects duplicates so a
     * second install_call from app code is safe. */
    safi_editor_shortcuts_register(&(SafiEditorChord){
        .key = SDL_SCANCODE_F1, .required_mods = 0,
        .callback = cb_toggle_play, .name = "Toggle Play",
    });
    safi_editor_shortcuts_register(&(SafiEditorChord){
        .key = SDL_SCANCODE_F6, .required_mods = 0,
        .callback = cb_snapshot, .name = "Snapshot Scratch",
    });
    safi_editor_shortcuts_register(&(SafiEditorChord){
        .key = SDL_SCANCODE_F7, .required_mods = 0,
        .callback = cb_restore, .name = "Restore Scratch",
    });
    safi_editor_shortcuts_register(&(SafiEditorChord){
        .key = SDL_SCANCODE_Z, .required_mods = SAFI_MOD_PRIMARY,
        .callback = cb_undo, .name = "Undo",
    });
    safi_editor_shortcuts_register(&(SafiEditorChord){
        .key = SDL_SCANCODE_Z,
        .required_mods = SAFI_MOD_PRIMARY | SDL_KMOD_SHIFT,
        .callback = cb_redo, .name = "Redo",
    });

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "safi_editor_shortcuts",
            .add  = ecs_ids(ecs_dependson(EcsOnUpdate)),
        }),
        .callback = editor_shortcuts_system,
    });
}
