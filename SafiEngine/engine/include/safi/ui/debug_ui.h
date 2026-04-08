/**
 * safi/ui/debug_ui.h — MicroUI debug overlay, rendered through SDL_gpu.
 *
 * Pure C. The backend lives in engine/src/ui/debug_ui.c and owns a MicroUI
 * context, stb_truetype font atlas, batched vertex/index buffers, and a
 * dedicated graphics pipeline. Widget code (mu_begin_window/.../mu_end_window)
 * goes between safi_debug_ui_begin_frame and safi_debug_ui_prepare.
 */
#ifndef SAFI_UI_DEBUG_UI_H
#define SAFI_UI_DEBUG_UI_H

#include <stdbool.h>
#include <flecs.h>
#include "safi/render/renderer.h"

typedef struct mu_Context mu_Context;

bool safi_debug_ui_init(SafiRenderer *r);
void safi_debug_ui_shutdown(SafiRenderer *r);

/* Forward an SDL_Event to MicroUI. Call from the input system. */
void safi_debug_ui_process_event(const void *sdl_event);

/* Begin a new MicroUI frame. Must be called before any widget calls. */
void safi_debug_ui_begin_frame(SafiRenderer *r);

/* Finalise MicroUI widgets, batch draw commands into vertex/index data,
 * and upload to the GPU. MUST be called BEFORE safi_renderer_begin_main_pass
 * — the backend opens a copy pass here, and SDL_gpu forbids nested passes. */
void safi_debug_ui_prepare(SafiRenderer *r);

/* Record MicroUI draw calls into the active render pass. Must be called
 * while the main render pass is open (i.e. after begin_main_pass, before
 * end_main_pass). */
void safi_debug_ui_render(SafiRenderer *r);

/* Draw the built-in Scene hierarchy (left) + Inspector (right) panels.
 * Queries the ECS world for named entities and displays editable
 * components for the currently selected entity. Call between
 * safi_debug_ui_begin_frame and safi_debug_ui_prepare. */
void safi_debug_ui_draw_panels(SafiRenderer *r, ecs_world_t *world);

/* Get / set the entity shown in the inspector. */
ecs_entity_t safi_debug_ui_selected_entity(void);
void         safi_debug_ui_select_entity(ecs_entity_t e);

/* Get the MicroUI context. NULL until safi_debug_ui_init has succeeded.
 * Include <microui.h> to access the full struct and call widget functions. */
mu_Context *safi_debug_ui_context(void);

/* Returns true when a MicroUI widget is active (e.g. editing a text field).
 * Game input systems should skip keyboard handling when this is true so
 * that typed characters go to the UI instead of moving the camera. */
bool safi_debug_ui_wants_input(void);

#endif /* SAFI_UI_DEBUG_UI_H */
