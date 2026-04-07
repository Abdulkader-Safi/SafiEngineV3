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

/* Nuklear: the engine's debug_ui.c owns NK_IMPLEMENTATION; this TU only
 * needs the struct definitions and widget functions — the same feature
 * macros must be set for struct layout to match across translation units. */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include <nuklear.h>

#include <stdio.h>
#include <string.h>

typedef struct DemoState {
    SafiMesh      mesh;
    SafiMaterial  material;
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

    SafiTransform *xform = ecs_get_mut(it->world, g_demo.model_entity, SafiTransform);
    if (!xform) return;

    float dt = it->delta_time;
    float rate = 1.5f; /* rad/s */

    /* Apply a delta quaternion to xform->rotation in world space.
     * IMPORTANT: cglm's glm_quat_mul does not safely alias dest with the
     * second operand — it writes intermediate components while still reading
     * the original, which corrupts the result. Use a local tmp and copy
     * back. Normalize afterwards to prevent drift across many frames. */
    #define APPLY_DELTA(angle, ax, ay, az) do {                         \
        versor _q, _tmp;                                                \
        glm_quatv(_q, (angle), (vec3){(ax),(ay),(az)});                 \
        glm_quat_mul(_q, xform->rotation, _tmp);                        \
        glm_quat_copy(_tmp, xform->rotation);                           \
    } while (0)

    /* Arrows → yaw / pitch (world-space) */
    if (in->keys[SDL_SCANCODE_LEFT])  APPLY_DELTA( rate*dt, 0,1,0);
    if (in->keys[SDL_SCANCODE_RIGHT]) APPLY_DELTA(-rate*dt, 0,1,0);
    if (in->keys[SDL_SCANCODE_UP])    APPLY_DELTA( rate*dt, 1,0,0);
    if (in->keys[SDL_SCANCODE_DOWN])  APPLY_DELTA(-rate*dt, 1,0,0);

    /* A / D → roll */
    if (in->keys[SDL_SCANCODE_A])     APPLY_DELTA( rate*dt, 0,0,1);
    if (in->keys[SDL_SCANCODE_D])     APPLY_DELTA(-rate*dt, 0,0,1);

    #undef APPLY_DELTA

    glm_quat_normalize(xform->rotation);

    /* W / S → dolly camera */
    SafiCamera *cam = ecs_get_mut(it->world, g_demo.camera_entity, SafiCamera);
    if (cam) {
        if (in->keys[SDL_SCANCODE_W]) cam->target[2] -= 2.0f * dt;
        if (in->keys[SDL_SCANCODE_S]) cam->target[2] += 2.0f * dt;
    }

    ecs_modified(it->world, g_demo.model_entity, SafiTransform);
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
    SafiTransform *xf  = ecs_get_mut(it->world, g_demo.model_entity,  SafiTransform);
    if (!cam || !xf) { safi_renderer_end_frame(r); return; }

    /* ---- Build Nuklear widgets (pre-pass) ------------------------------ */
    if (a->debug_ui_enabled) {
        safi_debug_ui_begin_frame(r);
        struct nk_context *ctx = safi_debug_ui_context();
        if (ctx && nk_begin(ctx, "SafiEngine",
                            nk_rect(20, 20, 300, 520),
                            NK_WINDOW_BORDER | NK_WINDOW_MOVABLE |
                            NK_WINDOW_SCALABLE | NK_WINDOW_TITLE)) {
            char buf[64];

            nk_layout_row_dynamic(ctx, 18, 1);
            snprintf(buf, sizeof(buf), "Backend: %s", safi_renderer_backend_name(r));
            nk_label(ctx, buf, NK_TEXT_LEFT);
            snprintf(buf, sizeof(buf), "FPS: %.1f",
                     1.0f / (it->delta_time > 0 ? it->delta_time : 1.0f));
            nk_label(ctx, buf, NK_TEXT_LEFT);

            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "Position", NK_TEXT_LEFT);
            nk_property_float(ctx, "pos x", -100.0f, &xf->position[0], 100.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "pos y", -100.0f, &xf->position[1], 100.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "pos z", -100.0f, &xf->position[2], 100.0f, 0.1f, 0.01f);

            /* Extract euler angles (radians) from the quaternion, convert to
             * degrees for the UI, then convert back after editing. */
            mat4 rot_mat;
            vec3 euler_rad;
            glm_quat_mat4(xf->rotation, rot_mat);
            glm_euler_angles(rot_mat, euler_rad);
            float rot_deg[3] = {
                glm_deg(euler_rad[0]),
                glm_deg(euler_rad[1]),
                glm_deg(euler_rad[2]),
            };

            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "Rotation", NK_TEXT_LEFT);
            nk_property_float(ctx, "rot x", -180.0f, &rot_deg[0], 180.0f, 1.0f, 0.5f);
            nk_property_float(ctx, "rot y", -180.0f, &rot_deg[1], 180.0f, 1.0f, 0.5f);
            nk_property_float(ctx, "rot z", -180.0f, &rot_deg[2], 180.0f, 1.0f, 0.5f);

            /* Write modified euler angles back to the quaternion. */
            euler_rad[0] = glm_rad(rot_deg[0]);
            euler_rad[1] = glm_rad(rot_deg[1]);
            euler_rad[2] = glm_rad(rot_deg[2]);
            glm_euler_xyz(euler_rad, rot_mat);
            glm_mat4_quat(rot_mat, xf->rotation);

            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "Scale", NK_TEXT_LEFT);
            nk_property_float(ctx, "scale x", 0.01f, &xf->scale[0], 100.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "scale y", 0.01f, &xf->scale[1], 100.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "scale z", 0.01f, &xf->scale[2], 100.0f, 0.1f, 0.01f);
        }
        if (ctx) nk_end(ctx);

        safi_debug_ui_prepare(r);  /* runs its own copy pass — must be pre-pass */
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

    SDL_BindGPUGraphicsPipeline(r->pass, g_demo.material.pipeline);
    SDL_GPUBufferBinding vbind = { .buffer = g_demo.mesh.vbo, .offset = 0 };
    SDL_BindGPUVertexBuffers(r->pass, 0, &vbind, 1);
    SDL_GPUBufferBinding ibind = { .buffer = g_demo.mesh.ibo, .offset = 0 };
    SDL_BindGPUIndexBuffer(r->pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    /* Vertex uniform slot 0 -> SDL_GPU vertex UBO slot 0. See unlit.hlsl. */
    SDL_PushGPUVertexUniformData(r->cmd, 0, &mvp, sizeof(mvp));

    if (g_demo.material.base_color) {
        SDL_GPUTextureSamplerBinding sbind = {
            .texture = g_demo.material.base_color,
            .sampler = g_demo.material.sampler,
        };
        SDL_BindGPUFragmentSamplers(r->pass, 0, &sbind, 1);
    }

    SDL_DrawGPUIndexedPrimitives(r->pass, g_demo.mesh.index_count, 1, 0, 0, 0);

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

    /* Load the demo material + glTF model. Compiled shaders live in the
     * build tree at SAFI_DEMO_SHADER_DIR (see examples/gltf_viewer/CMakeLists.txt);
     * the loader picks .spv or .msl based on the active GPU backend. */
    char model_path[1024];
    snprintf(model_path,  sizeof(model_path),  "%s/models/BoxTextured.glb", SAFI_DEMO_ASSET_DIR);

    if (!safi_material_create_unlit(&app.renderer, &g_demo.material, SAFI_DEMO_SHADER_DIR)) {
        SAFI_LOG_ERROR("failed to build unlit material");
        return 2;
    }
    if (!safi_gltf_load(&app.renderer, model_path, &g_demo.mesh, &g_demo.material)) {
        SAFI_LOG_ERROR("failed to load %s", model_path);
        return 3;
    }

    /* Spawn the model entity. */
    g_demo.model_entity = ecs_new(world);
    ecs_set(world, g_demo.model_entity, SafiTransform, {
        .position = {0.0f, 0.0f, 0.0f},
        .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
        .scale    = {1.0f, 1.0f, 1.0f},
    });
    ecs_set(world, g_demo.model_entity, SafiMeshRenderer, {
        .mesh     = &g_demo.mesh,
        .material = &g_demo.material,
    });
    ecs_set(world, g_demo.model_entity, SafiName, { .value = "BoxTextured" });

    /* Spawn the camera. */
    g_demo.camera_entity = ecs_new(world);
    ecs_set(world, g_demo.camera_entity, SafiCamera, {
        .fov_y_radians = 1.0472f, /* 60° */
        .z_near        = 0.1f,
        .z_far         = 100.0f,
        .target        = {0, 0, 0},
    });

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

    safi_material_destroy(&app.renderer, &g_demo.material);
    safi_mesh_destroy(&app.renderer, &g_demo.mesh);
    safi_app_shutdown(&app);
    return 0;
}
