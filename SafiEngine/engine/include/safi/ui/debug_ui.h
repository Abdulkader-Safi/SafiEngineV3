/**
 * safi/ui/debug_ui.h — Dear ImGui integration via cimgui.
 *
 * Engine code stays pure C; the C++ backend bridge lives inside the
 * safi_cimgui library.
 */
#ifndef SAFI_UI_DEBUG_UI_H
#define SAFI_UI_DEBUG_UI_H

#include <stdbool.h>
#include "safi/render/renderer.h"

bool safi_debug_ui_init(SafiRenderer *r);
void safi_debug_ui_shutdown(SafiRenderer *r);

/* Forward an SDL_Event to ImGui. Call from the input system. */
void safi_debug_ui_process_event(const void *sdl_event);

/* Begin a new ImGui frame. Must be called before any ImGui widget calls. */
void safi_debug_ui_begin_frame(SafiRenderer *r);

/* Record ImGui draw data into the active render pass of `r`. Must be called
 * between safi_renderer_begin_frame and safi_renderer_end_frame. */
void safi_debug_ui_render(SafiRenderer *r);

#endif /* SAFI_UI_DEBUG_UI_H */
