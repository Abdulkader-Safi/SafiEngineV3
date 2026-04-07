/*
 * gltf_viewer — SafiEngine demo.
 *
 * Loads a glTF model, registers a spin component, and lets the user rotate
 * and zoom the model with the keyboard:
 *
 *   Arrow Up/Down  → pitch
 *   Arrow Left/Right → yaw
 *   A / D          → roll
 *   W / S          → zoom (dolly along camera forward)
 *
 * A Nuklear overlay shows FPS and a transform inspector.
 */
#include <safi/safi.h>

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

typedef struct DemoState {
    SafiModel     model;
    ecs_entity_t  model_entity;
    ecs_entity_t  camera_entity;
} DemoState;

static DemoState g_demo;

/* ---- Systems ----------------------------------------------------------- */

static void control_system(ecs_iter_t *it) {
    /* Skip game keyboard controls when a Nuklear widget is active (e.g. the
     * user is typing a number into a property field). */
    if (safi_debug_ui_wants_input()) return;

    const SafiInput *in = ecs_singleton_get(it->world, SafiInput);
    if (!in) return;

    float dt = it->delta_time;
    float rate = 1.5f; /* rad/s */

    /* Rotate the model entity (arrows + A/D) regardless of selection. */
    SafiTransform *xform = ecs_get_mut(it->world, g_demo.model_entity, SafiTransform);
    if (xform) {
        #define APPLY_DELTA(angle, ax, ay, az) do {                         \
            versor _q, _tmp;                                                \
            glm_quatv(_q, (angle), (vec3){(ax),(ay),(az)});                 \
            glm_quat_mul(_q, xform->rotation, _tmp);                        \
            glm_quat_copy(_tmp, xform->rotation);                           \
        } while (0)

        if (in->keys[SDL_SCANCODE_LEFT])  APPLY_DELTA( rate*dt, 0,1,0);
        if (in->keys[SDL_SCANCODE_RIGHT]) APPLY_DELTA(-rate*dt, 0,1,0);
        if (in->keys[SDL_SCANCODE_UP])    APPLY_DELTA( rate*dt, 1,0,0);
        if (in->keys[SDL_SCANCODE_DOWN])  APPLY_DELTA(-rate*dt, 1,0,0);
        if (in->keys[SDL_SCANCODE_A])     APPLY_DELTA( rate*dt, 0,0,1);
        if (in->keys[SDL_SCANCODE_D])     APPLY_DELTA(-rate*dt, 0,0,1);

        #undef APPLY_DELTA

        glm_quat_normalize(xform->rotation);
        ecs_modified(it->world, g_demo.model_entity, SafiTransform);
    }

    /* W / S → dolly camera */
    SafiCamera *cam = ecs_get_mut(it->world, g_demo.camera_entity, SafiCamera);
    if (cam) {
        if (in->keys[SDL_SCANCODE_W]) cam->target[2] -= 2.0f * dt;
        if (in->keys[SDL_SCANCODE_S]) cam->target[2] += 2.0f * dt;
    }
}

/* Render system: draws the glTF model + Nuklear debug UI.
 *
 * Frame shape (SDL_gpu forbids nested passes, so the UI's vertex upload
 * must happen BEFORE the main render pass opens):
 *
 *   begin_frame                    -> cmd + swapchain
 *   debug_ui_begin_frame           -> NK ready for widget calls
 *   <build widgets>
 *   debug_ui_prepare               -> nk_convert + copy-pass upload
 *   begin_main_pass                -> open color+depth pass
 *   <draw mesh>
 *   debug_ui_render                -> record NK draws into the pass
 *   end_main_pass
 *   end_frame                      -> submit
 */
static void render_system(ecs_iter_t *it) {
    extern SafiApp *g_app;
    SafiApp *a = (SafiApp *)it->ctx;
    if (!a) a = g_app;

    SafiRenderer *r = &a->renderer;
    if (!safi_renderer_begin_frame(r)) return;

    SafiCamera    *cam = ecs_get_mut(it->world, g_demo.camera_entity, SafiCamera);
    SafiTransform *xf  = ecs_get_mut(it->world, g_demo.model_entity, SafiTransform);
    if (!cam || !xf) { safi_renderer_end_frame(r); return; }

    /* ---- Build Nuklear widgets (pre-pass) ------------------------------ */
    if (a->debug_ui_enabled) {
        safi_debug_ui_begin_frame(r);
        safi_debug_ui_draw_panels(r, it->world);
        safi_debug_ui_prepare(r);
    }

    /* ---- Main render pass --------------------------------------------- */
    safi_renderer_begin_main_pass(r);

    mat4 view, proj, model, mvp;
    vec3 eye = { cam->target[0], cam->target[1], cam->target[2] + 3.0f };
    vec3 center = { 0, 0, 0 };
    vec3 up = { 0, 1, 0 };
    glm_lookat(eye, center, up, view);

    float aspect = (float)r->swapchain_w / (float)r->swapchain_h;
    /* cglm is built with CGLM_FORCE_DEPTH_ZERO_TO_ONE (see cmake/
     * Dependencies.cmake) so glm_perspective emits the [0,1] clip-space Z
     * that SDL_gpu expects on Metal / Vulkan / D3D12. */
    glm_perspective(cam->fov_y_radians, aspect, cam->z_near, cam->z_far, proj);

    glm_mat4_identity(model);
    glm_translate(model, xf->position);
    mat4 rot;
    glm_quat_mat4(xf->rotation, rot);
    glm_mat4_mul(model, rot, model);
    glm_scale(model, xf->scale);

    glm_mat4_mul(proj, view, mvp);
    glm_mat4_mul(mvp, model, mvp);

    /* Reset viewport + scissor to the full target. Safer than assuming
     * SDL_gpu inherited defaults; also resets anything Nuklear left behind
     * in the previous frame's pass. */
    SDL_GPUViewport vp = {
        .x = 0.0f, .y = 0.0f,
        .w = (float)r->swapchain_w, .h = (float)r->swapchain_h,
        .min_depth = 0.0f, .max_depth = 1.0f,
    };
    SDL_SetGPUViewport(r->pass, &vp);
    SDL_Rect full = { 0, 0, (int)r->swapchain_w, (int)r->swapchain_h };
    SDL_SetGPUScissor(r->pass, &full);

    safi_model_draw(r, &g_demo.model, (const float *)mvp);

    if (a->debug_ui_enabled) safi_debug_ui_render(r);

    safi_renderer_end_main_pass(r);
    safi_renderer_end_frame(r);
}

/* ---- main -------------------------------------------------------------- */

SafiApp *g_app = NULL;

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    SafiApp app;
    SafiAppDesc desc = {
        .title            = "SafiEngine — glTF Viewer",
        .width            = 1280,
        .height           = 720,
        .vsync            = true,
        .enable_debug_ui  = true,
    };
    if (!safi_app_init(&app, &desc)) return 1;
    g_app = &app;

    ecs_world_t *world = safi_app_world(&app);

    /* Load the glTF model with all primitives and per-material textures.
     * Compiled shaders live in SAFI_DEMO_SHADER_DIR (see CMakeLists.txt);
     * the loader picks .spv or .msl based on the active GPU backend. */
    char model_path[1024];
    snprintf(model_path,  sizeof(model_path),  "%s/models/BoxTextured.glb", SAFI_DEMO_ASSET_DIR);
    // snprintf(model_path, sizeof(model_path), "%s/models/player.glb", SAFI_DEMO_ASSET_DIR);

    if (!safi_model_load(&app.renderer, model_path, SAFI_DEMO_SHADER_DIR, &g_demo.model)) {
        SAFI_LOG_ERROR("failed to load %s", model_path);
        return 2;
    }

    /* Spawn the model entity. */
    g_demo.model_entity = ecs_new(world);
    ecs_set(world, g_demo.model_entity, SafiTransform, {
        .position = {0.0f, 0.0f, 0.0f},
        .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
        .scale    = {1.0f, 1.0f, 1.0f},
    });
    ecs_set(world, g_demo.model_entity, SafiName, { .value = "Model" });

    /* Spawn the camera. */
    g_demo.camera_entity = ecs_new(world);
    ecs_set(world, g_demo.camera_entity, SafiCamera, {
        .fov_y_radians = 1.0472f, /* 60° */
        .z_near        = 0.1f,
        .z_far         = 100.0f,
        .target        = {0, 0, 0},
    });
    ecs_set(world, g_demo.camera_entity, SafiName, { .value = "Camera" });

    /* Default selection for the inspector. */
    safi_debug_ui_select_entity(g_demo.model_entity);

    /* Register user systems. Store app ptr in ctx so render_system can
     * reach the renderer. */
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "control_system",
                                      .add = ecs_ids(ecs_dependson(EcsOnUpdate)) }),
        .callback = control_system,
    });
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "render_system",
                                      .add = ecs_ids(ecs_dependson(EcsOnStore)) }),
        .callback = render_system,
        .ctx      = &app,
    });

    safi_app_run(&app);

    safi_model_destroy(&app.renderer, &g_demo.model);
    safi_app_shutdown(&app);
    return 0;
}
