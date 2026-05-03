/**
 * safi/ui/inspector_widgets.h — reusable MicroUI inspector property widgets.
 *
 * Extracted from debug_ui.c so the component registry's per-component
 * inspector draw callbacks can use them without depending on debug_ui
 * internals. Each widget is a self-contained row (label + interactive
 * cells).
 */
#ifndef SAFI_UI_INSPECTOR_WIDGETS_H
#define SAFI_UI_INSPECTOR_WIDGETS_H

#include <stdbool.h>
#include <stdint.h>
#include <microui.h>

void safi_inspector_number_cell    (mu_Context *ctx, float *val, float step);
void safi_inspector_property_float (mu_Context *ctx, const char *label,
                                    float *val, float step);
void safi_inspector_property_vec3  (mu_Context *ctx, const char *label,
                                    float *xyz, float step);
void safi_inspector_property_color_rgba(mu_Context *ctx, const char *label,
                                        float rgba[4], float step);
/* Returns the underlying mu_textbox_ex result: MU_RES_CHANGE while the
 * user is editing, MU_RES_SUBMIT on Enter, 0 otherwise. Callers that need
 * "commit on Enter" semantics (e.g. loading an asset on path change)
 * should only apply changes when MU_RES_SUBMIT is set. */
int  safi_inspector_property_string(mu_Context *ctx, const char *label,
                                    char *buf, int cap);
void safi_inspector_property_bool  (mu_Context *ctx, const char *label,
                                    bool *val);
void safi_inspector_property_enum  (mu_Context *ctx, const char *label,
                                    int *value, const char *const *names,
                                    int count);

/* ---- Free-floating popups ----------------------------------------------- */

/* Single-frame popup picker. Pass a unique `name` per logical popup, a
 * one-shot `open_trigger` bool (typically the return value of a button
 * the caller drew earlier this frame), and an items list. Returns the
 * selected index >=0 exactly once on commit; -1 when still open or idle.
 * Anchors directly below the most-recently-laid-out widget (`ctx->last_rect`).
 * Call after the parent window's `mu_end_window` so the popup isn't
 * clipped to the window's body. Use for "+ Add Component", create-entity
 * pickers, etc. */
int safi_ui_popup_picker(mu_Context *ctx,
                         const char *name,
                         bool        open_trigger,
                         const char *const *items,
                         int         item_count);

/* Like safi_ui_popup_picker but anchored at the mouse cursor. `open_trigger`
 * is typically `mu_mouse_over(ctx, last_rect) && (ctx->mouse_pressed & MU_MOUSE_RIGHT)`. */
int safi_ui_context_menu(mu_Context *ctx,
                         const char *name,
                         bool        open_trigger,
                         const char *const *items,
                         int         item_count);

/* ---- Drag-and-drop ------------------------------------------------------ */

/* Mark the most-recently-laid-out widget as a drag source. Begins a drag
 * when the user mouse-presses while hovering. `tag` is a short string the
 * drop target matches against (e.g. "texture", "entity"); `value` is the
 * payload (asset handle id, entity id, etc.). Only one drag is active at
 * a time — beginning a new drag overwrites the previous one. */
void safi_ui_drag_source(mu_Context *ctx, const char *tag, uint32_t value);

/* Mark the most-recently-laid-out widget as a drop target. Returns true
 * exactly once on the frame the user releases the mouse over this widget
 * with a matching `tag` payload, writing the payload into `*out_value`.
 * Returns false otherwise. */
bool safi_ui_drop_target(mu_Context *ctx, const char *tag, uint32_t *out_value);

#endif /* SAFI_UI_INSPECTOR_WIDGETS_H */
