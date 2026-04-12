/**
 * safi/render/render_system.h — engine-owned per-frame render system.
 *
 * Registered automatically by safi_app_init on EcsOnStore. Queries for
 * (SafiGlobalTransform, SafiMeshRenderer) to find what to draw and
 * (SafiCamera, SafiActiveCamera) for the viewpoint. Also orchestrates
 * the MicroUI debug UI lifecycle (begin_frame / draw_panels / prepare /
 * render) when debug_ui_enabled is set.
 */
#ifndef SAFI_RENDER_RENDER_SYSTEM_H
#define SAFI_RENDER_RENDER_SYSTEM_H

#include <flecs.h>

typedef struct SafiApp SafiApp;

/* Register the engine's default render system on EcsOnStore.
 * Called automatically by safi_app_init after the renderer and debug UI
 * are initialized. The SafiApp pointer is stored as system ctx. */
void safi_render_system_init(ecs_world_t *world, SafiApp *app);

#endif /* SAFI_RENDER_RENDER_SYSTEM_H */
