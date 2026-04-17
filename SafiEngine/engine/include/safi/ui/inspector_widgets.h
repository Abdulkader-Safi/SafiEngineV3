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
#include <microui.h>

void safi_inspector_number_cell    (mu_Context *ctx, float *val, float step);
void safi_inspector_property_float (mu_Context *ctx, const char *label,
                                    float *val, float step);
void safi_inspector_property_vec3  (mu_Context *ctx, const char *label,
                                    float *xyz, float step);
void safi_inspector_property_color_rgba(mu_Context *ctx, const char *label,
                                        float rgba[4], float step);
void safi_inspector_property_string(mu_Context *ctx, const char *label,
                                    char *buf, int cap);
void safi_inspector_property_bool  (mu_Context *ctx, const char *label,
                                    bool *val);
void safi_inspector_property_enum  (mu_Context *ctx, const char *label,
                                    int *value, const char *const *names,
                                    int count);

#endif /* SAFI_UI_INSPECTOR_WIDGETS_H */
