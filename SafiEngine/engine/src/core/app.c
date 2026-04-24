#include "safi/core/app.h"
#include "safi/core/log.h"
#include "safi/core/time.h"
#include "safi/ecs/change_bus.h"
#include "safi/ecs/ecs.h"
#include "safi/ecs/components.h"
#include "safi/ecs/phases.h"
#include "safi/editor/editor_state.h"
#include "safi/editor/editor_camera.h"
#include "safi/editor/editor_gizmo.h"
#include "safi/editor/editor_shortcuts.h"
#include "safi/input/input.h"
#include "safi/render/assets.h"
#include "safi/physics/physics.h"
#include "safi/audio/audio.h"
#include "safi/render/primitive_system.h"
#include "safi/render/render_system.h"
#include "safi/render/gizmo.h"
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
    safi_assets_init(&app->renderer);

    /* Register the project root so asset paths stored in scene files stay
     * portable across machines. NULL → CWD at init time. */
    if (desc->project_root && desc->project_root[0]) {
        safi_assets_set_project_root(desc->project_root);
    } else {
        char *cwd = SDL_GetCurrentDirectory();
        if (cwd) {
            safi_assets_set_project_root(cwd);
            SDL_free(cwd);
        }
    }

    /* Register the shader root. Callers usually pass the CMake-emitted
     * build dir; NULL falls back to `<project_root>/shaders`. Once set,
     * safi_shader_load accepts NULL as shader_dir. */
    if (desc->shader_root && desc->shader_root[0]) {
        safi_assets_set_shader_root(desc->shader_root);
    } else {
        const char *root = safi_assets_project_root();
        if (root && root[0]) {
            char fallback[512];
            SDL_snprintf(fallback, sizeof(fallback), "%s/shaders", root);
            safi_assets_set_shader_root(fallback);
        }
    }

    app->world = safi_ecs_create();
    if (!app->world) {
        safi_renderer_shutdown(&app->renderer);
        return false;
    }

    /* Install stock singletons. */
    ecs_singleton_set(app->world, SafiInput, {0});
    ecs_singleton_set(app->world, SafiTime,  {0});
    ecs_singleton_set(app->world, SafiEditorState, {
        .mode            = SAFI_EDITOR_MODE_EDIT,
        .selected_tool   = SAFI_EDITOR_TOOL_SELECT,
        .selected_entity = 0,
    });

    if (desc->enable_debug_ui) {
        if (!safi_debug_ui_init(&app->renderer)) {
            SAFI_LOG_WARN("debug UI init failed; continuing without MicroUI");
        } else {
            app->debug_ui_enabled = true;
        }

        /* Gizmos piggyback on the debug UI toggle: in a non-debug build
         * nothing would ever enqueue a gizmo anyway, so skip the GPU
         * resources. Failure here is non-fatal. */
        if (!safi_gizmo_system_init(&app->renderer)) {
            SAFI_LOG_WARN("gizmo system init failed; continuing without gizmos");
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

    /* Change bus observes every serializable component. Installed after
     * physics so its two extra components (SafiRigidBody/SafiCollider)
     * are covered. Undo/redo subscribes later; in the meantime the bus
     * is dormant with zero subscribers. */
    safi_change_bus_install(app->world);

    /* Engine-owned systems: primitives build their GPU resources on
     * EcsPreStore; render consumes everything on EcsOnStore. */
    safi_primitive_system_init(app->world, app);
    safi_render_system_init(app->world, app);

    /* Editor fly-cam + F-key shortcuts. Only useful when the debug UI is
     * on — without it the app is a shipping build and nothing would drive
     * the camera or listen for F1 anyway. */
    if (app->debug_ui_enabled) {
        safi_editor_camera_install(app->world);
        safi_editor_shortcuts_install(app->world);
        safi_editor_gizmo_install(app->world);
    }

    /* Hot-reload defaults on in debug-UI builds (the editor use case) and
     * off in shipping builds. Callers who want it in a shipping build can
     * flip the flag explicitly. */
    app->hot_reload_enabled  = desc->enable_hot_reload || desc->enable_debug_ui;
    app->watch_accumulator   = 0.0f;

    app->running      = true;
    app->last_ticks_ns = SDL_GetTicksNS();
    return true;
}

void safi_app_shutdown(SafiApp *app) {
    if (!app) return;
    safi_audio_shutdown();
    safi_physics_shutdown();
    if (app->debug_ui_enabled) {
        safi_gizmo_system_destroy(&app->renderer);
        safi_debug_ui_shutdown(&app->renderer);
    }
    if (app->world)  safi_ecs_destroy(app->world);
    safi_assets_shutdown();
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
    safi_change_bus_advance_frame(app->frame_count);

    /* ---- Input → ECS ---------------------------------------------------- */
    safi_input_pump(app->world);
    const SafiInput *in = ecs_singleton_get(app->world, SafiInput);
    if (in && in->quit_requested) app->running = false;

    /* Audio: GC finished voices + republish listener snapshot. Cheap. */
    safi_audio_update(dt);

    /* Hot-reload poll. ~4 Hz is fast enough that a "save in editor, see
     * it here" round-trip feels immediate without spamming stat() calls
     * on every frame. */
    if (app->hot_reload_enabled) {
        app->watch_accumulator += dt;
        if (app->watch_accumulator >= 0.25f) {
            safi_assets_watch_tick();
            app->watch_accumulator = 0.0f;
        }
    }

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

    /* Editor mode controls whether gameplay advances. Edit and Paused skip
     * both the fixed pipeline (physics, deterministic simulation) and the
     * user-authored game pipeline; the variable + render pipelines always
     * run so input, the Inspector, and the viewport stay responsive. */
    const SafiEditorState *ed = ecs_singleton_get(app->world, SafiEditorState);
    bool game_active = (ed && ed->mode == SAFI_EDITOR_MODE_PLAY);

    /* ---- Stage 2: fixed-rate systems (SafiFixedUpdate) ------------------ *
     * Drain the accumulator with stable steps. Capped at fixed_max_steps
     * per frame to prevent spiral-of-death on frame stalls. */
    app->fixed_accumulator += dt;
    int steps = 0;
    if (game_active) {
        while (app->fixed_accumulator >= app->fixed_dt && steps < app->fixed_max_steps) {
            ecs_run_pipeline(app->world, safi_ecs_fixed_pipeline(), app->fixed_dt);
            app->fixed_accumulator -= app->fixed_dt;
            app->fixed_elapsed     += app->fixed_dt;
            steps++;
        }
    } else {
        /* Edit/Paused: drain the accumulator silently so the next Play frame
         * doesn't see a pathological backlog from the time spent paused. */
        app->fixed_accumulator = 0.0f;
    }
    /* If we hit the step cap with leftover dt, drop it — otherwise the next
     * frame will try to catch up and stall again. */
    if (steps == app->fixed_max_steps && app->fixed_accumulator >= app->fixed_dt) {
        app->fixed_accumulator = 0.0f;
    }

    /* ---- Stage 2b: variable-rate gameplay (SafiGamePhase) --------------- */
    if (game_active) {
        ecs_run_pipeline(app->world, safi_ecs_game_pipeline(), dt);
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
