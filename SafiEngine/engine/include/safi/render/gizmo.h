/**
 * safi/render/gizmo.h — editor gizmo draw list.
 *
 * Frame-lifetime buffer of world-space lines and wireframes. Any system,
 * from any phase, can enqueue draws by calling `safi_gizmo_draw_*`. The
 * render system uploads the queue before opening the main pass (copy passes
 * cannot run inside a render pass) and drains it with a single pipeline
 * inside the main pass. The queue clears at the end of every frame.
 *
 * The pipeline uses a tiny `pos + rgba` vertex format, a line-list
 * topology, blending enabled, and depth test on / depth write off — so
 * gizmos are occluded by solid geometry but don't corrupt depth for other
 * translucent passes.
 *
 * This module does NOT own the gizmo *entities* (translate/rotate/scale
 * handles). It owns the low-level draw primitive those handles are built
 * from.
 */
#ifndef SAFI_RENDER_GIZMO_H
#define SAFI_RENDER_GIZMO_H

#include <stdbool.h>
#include "safi/render/renderer.h"
#include "safi/render/light_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One-time setup at app startup. Creates the shader, pipeline, and VBO. */
bool safi_gizmo_system_init   (SafiRenderer *r);
void safi_gizmo_system_destroy(SafiRenderer *r);

/* Enqueue draws. Main thread only. Safe to call from any frame phase as
 * long as it's before `safi_gizmo_system_draw` runs. RGBA is linear 0..1. */
void safi_gizmo_draw_line        (const float a[3], const float b[3],
                                  const float rgba[4]);
void safi_gizmo_draw_box_wire    (const float center[3], const float half[3],
                                  const float rgba[4]);
void safi_gizmo_draw_aabb        (const float min[3],    const float max[3],
                                  const float rgba[4]);
void safi_gizmo_draw_sphere_wire (const float center[3], float radius,
                                  int segments, const float rgba[4]);

/* Called by the render system before `safi_renderer_begin_main_pass`.
 * Opens a copy pass on the current command buffer and uploads this
 * frame's vertices to the VBO. No-op when the queue is empty. */
void safi_gizmo_system_upload(SafiRenderer *r);

/* Called by the render system inside the main pass after meshes. Binds
 * the gizmo pipeline, pushes the view-proj UBO, and issues one draw call.
 * Clears the queue. No-op when the queue is empty. */
void safi_gizmo_system_draw(SafiRenderer *r, const SafiCameraBuffer *cam);

#ifdef __cplusplus
}
#endif

#endif /* SAFI_RENDER_GIZMO_H */
