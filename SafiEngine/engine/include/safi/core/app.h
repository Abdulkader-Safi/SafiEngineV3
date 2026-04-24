/**
 * safi/core/app.h — top-level application object.
 *
 * A SafiApp bundles the window, renderer, ECS world, and main loop. Apps
 * usually look like:
 *
 *     int main(void) {
 *         SafiApp app;
 *         safi_app_init(&app, &(SafiAppDesc){ .title = "Demo", ... });
 *         // register user components, systems, spawn entities...
 *         safi_app_run(&app);
 *         safi_app_shutdown(&app);
 *     }
 */
#ifndef SAFI_CORE_APP_H
#define SAFI_CORE_APP_H

#include <stdbool.h>
#include <stdint.h>
#include <flecs.h>

#include "safi/render/renderer.h"
#include "safi/ui/debug_ui.h"

typedef struct SafiAppDesc {
    const char *title;
    int         width;
    int         height;
    bool        vsync;
    bool        enable_debug_ui;
    /* Seconds per fixed-update step. 0 → default (1/60). */
    float       fixed_dt;
    /* Max fixed steps per frame to prevent spiral-of-death. 0 → default (4). */
    int         fixed_max_steps;
    /* Root directory against which relative asset paths resolve. Scene
     * files store paths in this form so they stay portable. NULL → current
     * working directory at app-init time. */
    const char *project_root;
    /* Directory containing compiled shader artifacts. Separate from
     * project_root because CMake writes shaders into the build tree, not
     * into the source assets folder. NULL → `{project_root}/shaders`. */
    const char *shader_root;
    /* When true, the engine polls file mtimes under the project root and
     * hot-reloads changed models/textures. Defaults to on in debug-UI
     * builds (editor), off otherwise. */
    bool        enable_hot_reload;
} SafiAppDesc;

typedef struct SafiApp {
    SafiRenderer  renderer;
    ecs_world_t  *world;
    bool          running;
    bool          debug_ui_enabled;
    bool          hot_reload_enabled;
    uint64_t      last_ticks_ns;
    float         elapsed;
    uint64_t      frame_count;
    /* Fixed timestep accumulator. Driven by safi_app_tick. */
    float         fixed_dt;
    float         fixed_accumulator;
    float         fixed_elapsed;
    int           fixed_max_steps;
    /* Accumulator for the asset-watcher throttle (only ticks ~4×/s). */
    float         watch_accumulator;
} SafiApp;

bool safi_app_init(SafiApp *app, const SafiAppDesc *desc);
void safi_app_shutdown(SafiApp *app);

/* Runs the main loop: progress the ECS world, render, repeat. Exits when
 * the window is closed or a system sets app->running = false. */
void safi_app_run(SafiApp *app);

/* Single-frame step. Useful for embedding the engine inside another loop or
 * for tests. Returns false when the app should exit. */
bool safi_app_tick(SafiApp *app);

/* Convenience accessor. */
static inline ecs_world_t *safi_app_world(SafiApp *app) { return app->world; }

#endif /* SAFI_CORE_APP_H */
