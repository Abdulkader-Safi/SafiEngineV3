/**
 * safi/editor/editor_shortcuts.h — engine-owned editor keybinds.
 *
 * Shortcut handling is built around a small registration table: each
 * `SafiEditorChord` pairs a scancode + modifier mask with a callback,
 * and the system on EcsOnUpdate fires the callback exactly once per
 * matching key-press while no MicroUI text field has focus.
 *
 * Stock keybinds installed by the engine on `safi_editor_shortcuts_install`:
 *   F1            — toggle Edit ↔ Play
 *   F6            — snapshot the world to an in-memory cJSON blob
 *   F7            — restore that blob onto the live entities
 *   Cmd/Ctrl-Z    — undo (drives `safi_undo_perform`)
 *   Cmd/Ctrl-⇧Z   — redo
 *   Q / W / E / R — tool select / translate / rotate / scale
 *                   (gated to Edit mode; suppressed while the right mouse
 *                   button is held so the fly-cam keeps WASD).
 *
 * Custom keybinds: call `safi_editor_shortcuts_register` after
 * `_install`. Up to 32 chords total, system + user combined. Returns
 * false if the table is full.
 *
 * Scene-file shortcuts (F5 save / F9 load) remain in user/demo code
 * because they need app-specific handle refresh after load.
 */
#ifndef SAFI_EDITOR_EDITOR_SHORTCUTS_H
#define SAFI_EDITOR_EDITOR_SHORTCUTS_H

#include <stdbool.h>
#include <stdint.h>
#include <flecs.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

/* Cross-platform "primary" command modifier:
 *   - macOS: ⌘ (SDL_KMOD_GUI)
 *   - everywhere else: Ctrl (SDL_KMOD_CTRL)
 *
 * Using this for app-level shortcuts (Cmd-Z / Ctrl-Z, Cmd-S / Ctrl-S)
 * gives users the muscle-memory key for their platform. */
#ifdef __APPLE__
  #define SAFI_MOD_PRIMARY SDL_KMOD_GUI
#else
  #define SAFI_MOD_PRIMARY SDL_KMOD_CTRL
#endif

typedef void (*SafiEditorChordFn)(ecs_world_t *world);

typedef struct SafiEditorChord {
    SDL_Scancode      key;
    /* Bitmask of SDL_KMOD_* values that must all be held; chord misses
     * if any other modifier (besides lock keys) is also pressed. 0 means
     * "no modifiers". L/R variants are normalized: SDL_KMOD_CTRL matches
     * either Ctrl key. */
    uint16_t          required_mods;
    SafiEditorChordFn callback;
    const char       *name;        /* for future keybind UI / logging */
} SafiEditorChord;

void safi_editor_shortcuts_install(ecs_world_t *world);

/* Append a chord to the table. Returns false on duplicate key+mods or
 * when the table is full. The chord struct is copied; the caller does
 * not need to keep the storage live. */
bool safi_editor_shortcuts_register(const SafiEditorChord *chord);

#endif /* SAFI_EDITOR_EDITOR_SHORTCUTS_H */
