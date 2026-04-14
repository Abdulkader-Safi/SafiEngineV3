#include "safi/core/app.h"
#include "safi/core/log.h"
#include "safi/core/time.h"
#include "safi/ecs/ecs.h"
#include "safi/ecs/components.h"
#include "safi/ecs/phases.h"
#include "safi/input/input.h"
#include "safi/physics/physics.h"
#include "safi/audio/audio.h"
#include "safi/render/render_system.h"
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
            SAFI_LOG_WARN("debug UI init failed; continuing without MicroUI");
        } else {
            app->debug_ui_enabled = true;
        }
    }

    /* Fixed-timestep defaults. */
    app->fixed_dt          = desc->fixed_dt        > 0.0f ? desc->fixed_dt        : (1.0f / 60.0f);
    app->fixed_max_steps   = desc->fixed_max_steps > 0    ? desc->fixed_max_steps : 4;
    app->fixed_accumulator = 0.0f;
    app->fixed_elapsed     = 0.0f;

    /* Register engine-owned physics system on SafiFixedUpdate. */
    if (!safi_physics_init(app->world)) {
        SAFI_LOG_WARN("physics init failed; continuing without Jolt");
    }

    /* Register engine-owned audio subsystem (miniaudio). Non-fatal — CI /
     * headless hosts may have no audio device. */
    if (!safi_audio_init(app->world)) {
        SAFI_LOG_WARN("audio init failed; continuing without miniaudio");
    }

    /* Register engine-owned render system on EcsOnStore. */
    safi_render_system_init(app->world, app);

    app->running      = true;
    app->last_ticks_ns = SDL_GetTicksNS();
    return true;
}

void safi_app_shutdown(SafiApp *app) {
    if (!app) return;
    safi_audio_shutdown();
    safi_physics_shutdown();
    if (app->debug_ui_enabled) safi_debug_ui_shutdown(&app->renderer);
    if (app->world)  safi_ecs_destroy(app->world);
    safi_renderer_shutdown(&app->renderer);
    memset(app, 0, sizeof(*app));
}

bool safi_app_tick(SafiApp *app) {
    /* ---- Compute delta time --------------------------------------------- */
    uint64_t now_ns = SDL_GetTicksNS();
    float dt = (float)((double)(now_ns - app->last_ticks_ns) / 1.0e9);
    app->last_ticks_ns = now_ns;
    app->elapsed += dt;
    app->frame_count += 1;

    /* ---- Input → ECS ---------------------------------------------------- */
    safi_input_pump(app->world);
    const SafiInput *in = ecs_singleton_get(app->world, SafiInput);
    if (in && in->quit_requested) app->running = false;

    /* Audio: GC finished voices + republish listener snapshot. Cheap. */
    safi_audio_update(dt);

    /* ---- Stage 1: variable-rate systems --------------------------------- *
     * OnLoad .. PostUpdate. User gameplay, input-driven logic, and (after
     * step 2) transform propagation run here at wall-clock dt. */
    SafiTime t_pre = {
        .delta         = dt,
        .elapsed       = app->elapsed,
        .frame_count   = app->frame_count,
        .fixed_delta   = app->fixed_dt,
        .fixed_elapsed = app->fixed_elapsed,
        .fixed_overshoot = app->fixed_accumulator,
    };
    ecs_singleton_set_ptr(app->world, SafiTime, &t_pre);

    ecs_run_pipeline(app->world, safi_ecs_variable_pipeline(), dt);

    /* ---- Stage 2: fixed-rate systems (SafiFixedUpdate) ------------------ *
     * Drain the accumulator with stable steps. Capped at fixed_max_steps
     * per frame to prevent spiral-of-death on frame stalls. */
    app->fixed_accumulator += dt;
    int steps = 0;
    while (app->fixed_accumulator >= app->fixed_dt && steps < app->fixed_max_steps) {
        ecs_run_pipeline(app->world, safi_ecs_fixed_pipeline(), app->fixed_dt);
        app->fixed_accumulator -= app->fixed_dt;
        app->fixed_elapsed     += app->fixed_dt;
        steps++;
    }
    /* If we hit the step cap with leftover dt, drop it — otherwise the next
     * frame will try to catch up and stall again. */
    if (steps == app->fixed_max_steps && app->fixed_accumulator >= app->fixed_dt) {
        app->fixed_accumulator = 0.0f;
    }

    /* Re-publish SafiTime with updated fixed_* fields so the render stage
     * (and the inspector) sees current values. */
    SafiTime t_post = {
        .delta           = dt,
        .elapsed         = app->elapsed,
        .frame_count     = app->frame_count,
        .fixed_delta     = app->fixed_dt,
        .fixed_elapsed   = app->fixed_elapsed,
        .fixed_overshoot = app->fixed_accumulator,
    };
    ecs_singleton_set_ptr(app->world, SafiTime, &t_post);

    /* ---- Stage 3: render ------------------------------------------------- *
     * PreStore + OnStore. Reads the fully-settled world state. */
    ecs_run_pipeline(app->world, safi_ecs_render_pipeline(), dt);

    return app->running;
}

void safi_app_run(SafiApp *app) {
    while (app->running && safi_app_tick(app)) { /* loop */ }
}
