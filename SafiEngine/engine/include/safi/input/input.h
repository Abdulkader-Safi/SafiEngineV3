/**
 * safi/input/input.h — keyboard state snapshot exposed as an ECS singleton.
 *
 * The engine's input_system (OnLoad phase) drains the SDL event queue and
 * writes the current keyboard state into this singleton. Gameplay systems
 * should read it — never call SDL_GetKeyboardState directly.
 */
#ifndef SAFI_INPUT_INPUT_H
#define SAFI_INPUT_INPUT_H

#include <stdbool.h>
#include <SDL3/SDL_scancode.h>

typedef struct SafiInput {
    /* Pressed-this-frame state. Index with SDL_Scancode values. */
    bool keys[SDL_SCANCODE_COUNT];
    /* True for the single frame the key went down. */
    bool keys_pressed[SDL_SCANCODE_COUNT];
    /* True for the single frame the key went up. */
    bool keys_released[SDL_SCANCODE_COUNT];

    float mouse_x;
    float mouse_y;
    float mouse_dx;
    float mouse_dy;
    bool  mouse_buttons[8];

    /* Mouse wheel delta for this frame (positive = scroll up/right). */
    float scroll_x;
    float scroll_y;

    /* Keyboard modifier mask (SDL_KMOD_CTRL, SDL_KMOD_SHIFT, etc.).
     * Refreshed every frame from SDL_GetModState(). */
    uint16_t modifiers;

    bool quit_requested;
} SafiInput;

static inline bool safi_input_is_down(const SafiInput *in, int scancode) {
    return in->keys[scancode];
}

#endif /* SAFI_INPUT_INPUT_H */
