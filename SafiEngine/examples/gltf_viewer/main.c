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
 * A cimgui overlay shows FPS and a transform inspector.
 */
#include <safi/safi.h>

#include <SDL3/SDL.h>
#include <cimgui.h>

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
    const SafiInput *in = ecs_singleton_get(it->world, SafiInput);
    if (!in) return;

    SafiTransform *xform = ecs_get_mut(it->world, g_demo.model_entity, SafiTransform);
    if (!xform) return;

    float dt = it->delta_time;
    float rate = 1.5f; /* rad/s */

    versor delta;
    glm_quat_identity(delta);

    /* Arrows → yaw / pitch */
    if (in->keys[SDL_SCANCODE_LEFT])  { versor q; glm_quatv(q,  rate*dt, (vec3){0,1,0}); glm_quat_mul(q, xform->rotation, xform->rotation); }
    if (in->keys[SDL_SCANCODE_RIGHT]) { versor q; glm_quatv(q, -rate*dt, (vec3){0,1,0}); glm_quat_mul(q, xform->rotation, xform->rotation); }
    if (in->keys[SDL_SCANCODE_UP])    { versor q; glm_quatv(q,  rate*dt, (vec3){1,0,0}); glm_quat_mul(q, xform->rotation, xform->rotation); }
    if (in->keys[SDL_SCANCODE_DOWN])  { versor q; glm_quatv(q, -rate*dt, (vec3){1,0,0}); glm_quat_mul(q, xform->rotation, xform->rotation); }

    /* A / D → roll */
    if (in->keys[SDL_SCANCODE_A]) { versor q; glm_quatv(q,  rate*dt, (vec3){0,0,1}); glm_quat_mul(q, xform->rotation, xform->rotation); }
    if (in->keys[SDL_SCANCODE_D]) { versor q; glm_quatv(q, -rate*dt, (vec3){0,0,1}); glm_quat_mul(q, xform->rotation, xform->rotation); }

    /* W / S → dolly camera */
    SafiCamera *cam = ecs_get_mut(it->world, g_demo.camera_entity, SafiCamera);
    if (cam) {
        if (in->keys[SDL_SCANCODE_W]) cam->target[2] -= 2.0f * dt;
        if (in->keys[SDL_SCANCODE_S]) cam->target[2] += 2.0f * dt;
    }

    ecs_modified(it->world, g_demo.model_entity, SafiTransform);
}

/* Render system: draws the glTF model + debug UI. */
static void render_system(ecs_iter_t *it) {
    SafiApp *app = (SafiApp *)it->param; /* we'll pass this via ctx below */
    (void)app;

    extern SafiApp *g_app; /* set in main */
    SafiApp *a = (SafiApp *)it->ctx;
    if (!a) a = g_app;

    SafiRenderer *r = &a->renderer;
    if (!safi_renderer_begin_frame(r)) return;

    /* Begin ImGui frame before any widgets. */
    if (a->debug_ui_enabled) safi_debug_ui_begin_frame(r);

    /* Camera math. */
    SafiCamera    *cam = ecs_get_mut(it->world, g_demo.camera_entity, SafiCamera);
    SafiTransform *xf  = ecs_get_mut(it->world, g_demo.model_entity,  SafiTransform);
    if (!cam || !xf) goto end_frame;

    mat4 view, proj, model, mvp;
    vec3 eye = { cam->target[0], cam->target[1], cam->target[2] + 3.0f };
    vec3 center = { 0, 0, 0 };
    vec3 up = { 0, 1, 0 };
    glm_lookat(eye, center, up, view);

    float aspect = (float)r->swapchain_w / (float)r->swapchain_h;
    glm_perspective(cam->fov_y_radians, aspect, cam->z_near, cam->z_far, proj);

    glm_mat4_identity(model);
    glm_translate(model, xf->position);
    mat4 rot;
    glm_quat_mat4(xf->rotation, rot);
    glm_mat4_mul(model, rot, model);
    glm_scale(model, xf->scale);

    glm_mat4_mul(proj, view, mvp);
    glm_mat4_mul(mvp, model, mvp);

    /* Draw the mesh. */
    SDL_BindGPUGraphicsPipeline(r->pass, g_demo.material.pipeline);
    SDL_GPUBufferBinding vbind = { .buffer = g_demo.mesh.vbo, .offset = 0 };
    SDL_BindGPUVertexBuffers(r->pass, 0, &vbind, 1);
    SDL_GPUBufferBinding ibind = { .buffer = g_demo.mesh.ibo, .offset = 0 };
    SDL_BindGPUIndexBuffer(r->pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    SDL_PushGPUVertexUniformData(r->cmd, 0, &mvp, sizeof(mvp));

    if (g_demo.material.base_color) {
        SDL_GPUTextureSamplerBinding sbind = {
            .texture = g_demo.material.base_color,
            .sampler = g_demo.material.sampler,
        };
        SDL_BindGPUFragmentSamplers(r->pass, 0, &sbind, 1);
    }

    SDL_DrawGPUIndexedPrimitives(r->pass, g_demo.mesh.index_count, 1, 0, 0, 0);

    /* Debug UI widgets. */
    if (a->debug_ui_enabled) {
        igBegin("SafiEngine", NULL, 0);
        igText("Backend: %s", safi_renderer_backend_name(r));
        igText("FPS: %.1f", 1.0f / (it->delta_time > 0 ? it->delta_time : 1.0f));
        igSeparator();
        igText("Model transform");
        igDragFloat3("position", xf->position, 0.01f, -10.0f, 10.0f, "%.2f", 0);
        igDragFloat4("rotation", xf->rotation, 0.01f, -1.0f, 1.0f, "%.3f", 0);
        igDragFloat3("scale",    xf->scale,    0.01f,  0.01f, 10.0f, "%.2f", 0);
        igEnd();

        safi_debug_ui_render(r);
    }

end_frame:
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

    /* Load the demo material + glTF model. */
    char shader_path[1024];
    char model_path[1024];
    snprintf(shader_path, sizeof(shader_path), "%s/shaders/unlit.hlsl", SAFI_DEMO_ASSET_DIR);
    snprintf(model_path,  sizeof(model_path),  "%s/models/BoxTextured.glb", SAFI_DEMO_ASSET_DIR);

    if (!safi_material_create_unlit(&app.renderer, &g_demo.material, shader_path)) {
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
