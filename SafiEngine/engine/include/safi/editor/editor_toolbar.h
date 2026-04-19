/**
 * safi/editor/editor_toolbar.h — Play/Pause/Stop + tool-selector row.
 *
 * The visual counterpart to `SafiEditorState`. A thin MicroUI strip at the
 * top of the viewport with three mode buttons (Play / Pause / Stop) and
 * four tool buttons (Select / Translate / Rotate / Scale). Clicks write
 * `SafiEditorState.mode` or `.selected_tool`; the current value is
 * indicated by a darker button background.
 *
 * The toolbar is drawn from inside `safi_debug_ui_draw_panels` when the
 * debug UI is enabled. Apps that roll their own top-of-frame chrome can
 * call `safi_editor_toolbar_draw` directly instead.
 */
#ifndef SAFI_EDITOR_EDITOR_TOOLBAR_H
#define SAFI_EDITOR_EDITOR_TOOLBAR_H

#include <flecs.h>

typedef struct mu_Context mu_Context;

/* Draw the toolbar strip at y=0, full viewport width. Call between
 * `mu_begin` and `mu_end` (i.e. between safi_debug_ui_begin_frame and
 * safi_debug_ui_prepare). `viewport_w` is the swapchain width in pixels
 * (NOT logical units — the MicroUI backend already compensates for DPI). */
void safi_editor_toolbar_draw(mu_Context *ctx, ecs_world_t *world,
                              int viewport_w);

#endif /* SAFI_EDITOR_EDITOR_TOOLBAR_H */
