/**
 * safi/ui/debug_ui.h — Nuklear debug overlay, rendered through SDL_gpu.
 *
 * Pure C. The backend lives in engine/src/ui/debug_ui.c and owns a Nuklear
 * context, font atlas, vertex/index buffers, and a dedicated graphics
 * pipeline. Widget code (nk_begin/nk_label/...) goes between
 * safi_debug_ui_begin_frame and safi_debug_ui_prepare.
 */
#ifndef SAFI_UI_DEBUG_UI_H
#define SAFI_UI_DEBUG_UI_H

#include <stdbool.h>
#include "safi/render/renderer.h"

/* Forward declaration — the full nk_context lives inside nuklear.h, which
 * the engine source includes with NK_IMPLEMENTATION. Users who want to add
 * widgets also include nuklear.h (without NK_IMPLEMENTATION) to get the
 * full struct layout. */
struct nk_context;

bool safi_debug_ui_init(SafiRenderer *r);
void safi_debug_ui_shutdown(SafiRenderer *r);

/* Forward an SDL_Event to ImGui. Call from the input system. */
void safi_debug_ui_process_event(const void *sdl_event);

/* Begin a new ImGui frame. Must be called before any ImGui widget calls. */
void safi_debug_ui_begin_frame(SafiRenderer *r);

/* Finalise ImGui widgets and upload its vertex/index buffers to the GPU.
 * MUST be called BEFORE safi_renderer_begin_main_pass — the SDL_gpu ImGui
 * backend opens its own copy pass here, and SDL_gpu forbids nested passes. */
void safi_debug_ui_prepare(SafiRenderer *r);

/* Record Nuklear draw calls into the active render pass. Must be called
 * while the main render pass is open (i.e. after begin_main_pass, before
 * end_main_pass). */
void safi_debug_ui_render(SafiRenderer *r);

/* Get the Nuklear context. NULL until safi_debug_ui_init has succeeded.
 * Include <nuklear.h> (with the same NK_* macros used by the engine) to
 * access the full struct and call widget functions. */
struct nk_context *safi_debug_ui_context(void);

#endif /* SAFI_UI_DEBUG_UI_H */
