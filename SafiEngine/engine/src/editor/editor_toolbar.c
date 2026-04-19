#include "safi/editor/editor_toolbar.h"
#include "safi/editor/editor_state.h"
#include "safi/scene/scene.h"
#include "safi/core/log.h"

#include <cJSON.h>
#include <microui.h>

/* Auto-snapshot captured by the toolbar when Play is clicked from Edit,
 * replayed when Stop is clicked. Distinct from the F6/F7 scratchpad in
 * editor_shortcuts.c — different UX contract, so distinct storage. Freed
 * on Stop. Leaks on process exit, acceptable for a single-slot cache. */
static cJSON *g_play_snapshot = NULL;

static mu_Color dim(mu_Color c, int amount) {
    int r = (int)c.r - amount; if (r < 0) r = 0;
    int g = (int)c.g - amount; if (g < 0) g = 0;
    int b = (int)c.b - amount; if (b < 0) b = 0;
    return (mu_Color){ (unsigned char)r, (unsigned char)g,
                       (unsigned char)b, c.a };
}

/* MicroUI has no toggle-button primitive, so darken MU_COLOR_BUTTON for
 * the duration of the button call when `active` is true. Hover/focus
 * colours keep working because they draw on top of the pushed base. */
static int toolbar_button(mu_Context *ctx, const char *label, int active) {
    int clicked;
    if (active) {
        mu_Color saved = ctx->style->colors[MU_COLOR_BUTTON];
        ctx->style->colors[MU_COLOR_BUTTON] = dim(saved, 60);
        clicked = mu_button(ctx, label);
        ctx->style->colors[MU_COLOR_BUTTON] = saved;
    } else {
        clicked = mu_button(ctx, label);
    }
    return clicked & MU_RES_SUBMIT;
}

/* -- Play / Pause / Stop transitions -------------------------------------- */

static void toolbar_request_play(ecs_world_t *world) {
    SafiEditorMode m = safi_editor_get_mode(world);
    if (m == SAFI_EDITOR_MODE_PLAY) return;

    /* Snapshot only on the Edit → Play edge. Resuming from Pause just
     * re-enables the pipelines; the world hasn't changed so there's
     * nothing to capture. */
    if (m == SAFI_EDITOR_MODE_EDIT) {
        if (g_play_snapshot) cJSON_Delete(g_play_snapshot);
        g_play_snapshot = safi_scene_snapshot_all(world);
        SAFI_LOG_INFO("editor: play — world snapshotted");
    }
    safi_editor_set_mode(world, SAFI_EDITOR_MODE_PLAY);
}

static void toolbar_request_pause(ecs_world_t *world) {
    if (safi_editor_get_mode(world) == SAFI_EDITOR_MODE_PLAY) {
        safi_editor_set_mode(world, SAFI_EDITOR_MODE_PAUSED);
    }
}

static void toolbar_request_stop(ecs_world_t *world) {
    if (g_play_snapshot) {
        safi_scene_restore_snapshot(world, g_play_snapshot);
        cJSON_Delete(g_play_snapshot);
        g_play_snapshot = NULL;
        SAFI_LOG_INFO("editor: stop — world restored");
    }
    safi_editor_set_mode(world, SAFI_EDITOR_MODE_EDIT);
}

/* -- Panel --------------------------------------------------------------- */

void safi_editor_toolbar_draw(mu_Context *ctx, ecs_world_t *world,
                              int viewport_w) {
    if (!ctx || !world) return;

    /* Chromeless full-width strip. Interactive on purpose — cursor should
     * register as "over the toolbar" so editor tools skip input that
     * lands on the bar. */
    int opt = MU_OPT_NOTITLE | MU_OPT_NOFRAME | MU_OPT_NORESIZE |
              MU_OPT_NOSCROLL | MU_OPT_NOCLOSE;

    if (!mu_begin_window_ex(ctx, "##toolbar",
                            mu_rect(0, 0, viewport_w, 36), opt)) {
        return;
    }

    SafiEditorMode mode = safi_editor_get_mode(world);
    SafiEditorTool tool = safi_editor_get_tool(world);

    /* Row: 3 mode buttons, spacer, 4 tool buttons. Widths tuned for the
     * default font at 100 % DPI; MicroUI's logical-unit layout scales
     * consistently across HiDPI. */
    mu_layout_row(ctx, 8,
                  (int[]){ 64, 64, 64, 16, 64, 72, 64, 64 }, 24);

    if (toolbar_button(ctx, "Play",  mode == SAFI_EDITOR_MODE_PLAY)) {
        toolbar_request_play(world);
    }
    if (toolbar_button(ctx, "Pause", mode == SAFI_EDITOR_MODE_PAUSED)) {
        toolbar_request_pause(world);
    }
    if (toolbar_button(ctx, "Stop",  mode == SAFI_EDITOR_MODE_EDIT)) {
        toolbar_request_stop(world);
    }

    mu_label(ctx, "");  /* spacer */

    if (toolbar_button(ctx, "Select",    tool == SAFI_EDITOR_TOOL_SELECT)) {
        safi_editor_set_tool(world, SAFI_EDITOR_TOOL_SELECT);
    }
    if (toolbar_button(ctx, "Translate", tool == SAFI_EDITOR_TOOL_TRANSLATE)) {
        safi_editor_set_tool(world, SAFI_EDITOR_TOOL_TRANSLATE);
    }
    if (toolbar_button(ctx, "Rotate",    tool == SAFI_EDITOR_TOOL_ROTATE)) {
        safi_editor_set_tool(world, SAFI_EDITOR_TOOL_ROTATE);
    }
    if (toolbar_button(ctx, "Scale",     tool == SAFI_EDITOR_TOOL_SCALE)) {
        safi_editor_set_tool(world, SAFI_EDITOR_TOOL_SCALE);
    }

    mu_end_window(ctx);
}
