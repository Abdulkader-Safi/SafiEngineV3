#include "safi/input/input.h"
#include "safi/ecs/components.h"
#include "safi/ui/debug_ui.h"

#include <SDL3/SDL.h>
#include <string.h>

/* Drains SDL events and updates the SafiInput singleton on the world. */
void safi_input_pump(ecs_world_t *world) {
    SafiInput *in = ecs_singleton_ensure(world, SafiInput);
    if (!in) return;

    /* Reset edge state each frame. */
    memset(in->keys_pressed,  0, sizeof(in->keys_pressed));
    memset(in->keys_released, 0, sizeof(in->keys_released));
    in->mouse_dx = 0.0f;
    in->mouse_dy = 0.0f;
    in->scroll_x = 0.0f;
    in->scroll_y = 0.0f;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        safi_debug_ui_process_event(&e);

        switch (e.type) {
        case SDL_EVENT_QUIT:
            in->quit_requested = true;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.scancode < SDL_SCANCODE_COUNT && !e.key.repeat) {
                if (!in->keys[e.key.scancode]) in->keys_pressed[e.key.scancode] = true;
                in->keys[e.key.scancode] = true;
            }
            break;
        case SDL_EVENT_KEY_UP:
            if (e.key.scancode < SDL_SCANCODE_COUNT) {
                in->keys[e.key.scancode] = false;
                in->keys_released[e.key.scancode] = true;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            in->mouse_x  = e.motion.x;
            in->mouse_y  = e.motion.y;
            in->mouse_dx += e.motion.xrel;
            in->mouse_dy += e.motion.yrel;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            in->scroll_x += e.wheel.x;
            in->scroll_y += e.wheel.y;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button < 8) {
                in->mouse_buttons[e.button.button] =
                    (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            }
            break;
        default: break;
        }
    }

    in->modifiers = (uint16_t)SDL_GetModState();

    ecs_singleton_modified(world, SafiInput);
}
