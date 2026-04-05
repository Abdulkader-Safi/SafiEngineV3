#include "safi/core/app.h"
#include "safi/core/log.h"
#include "safi/ecs/ecs.h"
#include "safi/ecs/components.h"
#include "safi/input/input.h"
#include "safi/ui/debug_ui.h"

#include <SDL3/SDL.h>
#include <string.h>

/* Provided by engine/src/input/input.c */
void safi_input_pump(ecs_world_t *world);

bool safi_app_init(SafiApp *app, const SafiAppDesc *desc) {
    memset(app, 0, sizeof(*app));

    SafiRendererDesc rd = {
        .title  = desc->title,
        .width  = desc->width  ? desc->width  : 1280,
        .height = desc->height ? desc->height : 720,
        .vsync  = desc->vsync,
    };
    if (!safi_renderer_init(&app->renderer, &rd)) return false;

    app->world = safi_ecs_create();
    if (!app->world) {
        safi_renderer_shutdown(&app->renderer);
        return false;
    }

    /* Install stock singletons. */
    ecs_singleton_set(app->world, SafiInput, {0});
    ecs_singleton_set(app->world, SafiTime,  {0});

    if (desc->enable_debug_ui) {
        if (!safi_debug_ui_init(&app->renderer)) {
            SAFI_LOG_WARN("debug UI init failed; continuing without ImGui");
        } else {
            app->debug_ui_enabled = true;
        }
    }

    app->running      = true;
    app->last_ticks_ns = SDL_GetTicksNS();
    return true;
}

void safi_app_shutdown(SafiApp *app) {
    if (!app) return;
    if (app->debug_ui_enabled) safi_debug_ui_shutdown(&app->renderer);
    if (app->world)  safi_ecs_destroy(app->world);
    safi_renderer_shutdown(&app->renderer);
    memset(app, 0, sizeof(*app));
}

bool safi_app_tick(SafiApp *app) {
    /* Compute delta time. */
    uint64_t now_ns = SDL_GetTicksNS();
    float dt = (float)((double)(now_ns - app->last_ticks_ns) / 1.0e9);
    app->last_ticks_ns = now_ns;
    app->elapsed += dt;
    app->frame_count += 1;

    SafiTime t = {
        .delta       = dt,
        .elapsed     = app->elapsed,
        .frame_count = app->frame_count,
    };
    ecs_singleton_set_ptr(app->world, ecs_id(SafiTime), &t);

    /* Input → ECS */
    safi_input_pump(app->world);
    const SafiInput *in = ecs_singleton_get(app->world, SafiInput);
    if (in && in->quit_requested) app->running = false;

    /* User systems run here via flecs pipeline. */
    ecs_progress(app->world, dt);

    return app->running;
}

void safi_app_run(SafiApp *app) {
    while (app->running && safi_app_tick(app)) { /* loop */ }
}
