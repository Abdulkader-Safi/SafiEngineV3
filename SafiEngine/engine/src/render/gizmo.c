/*
 * gizmo.c — editor gizmo line-list draw buffer.
 *
 * Same shape as debug_ui: CPU-side vertex staging + a per-frame transfer
 * buffer → VBO copy, then one draw call inside the main pass. The queue is
 * global (one list per app) so any system can enqueue without plumbing a
 * context through flecs.
 */

#include "safi/render/gizmo.h"
#include "safi/render/shader.h"
#include "safi/core/log.h"

#include <SDL3/SDL.h>
#include <cglm/cglm.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* -- tuning --------------------------------------------------------------- */
#define GIZMO_VBO_BYTES (128 * 1024)
#define GIZMO_MAX_VERTS (GIZMO_VBO_BYTES / (int)sizeof(GizmoVertex))

typedef struct GizmoVertex {
    float pos[3];
    float rgba[4];
} GizmoVertex;

static struct {
    bool initialized;

    SDL_Window              *window;
    SDL_GPUDevice           *device;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUBuffer           *vbo;

    GizmoVertex *verts;
    int          vert_count;
    int          vert_cap;

    /* Per-frame byte count uploaded by safi_gizmo_system_upload; read by
     * safi_gizmo_system_draw to issue the matching draw call. */
    int          uploaded_verts;

    bool         overflow_logged_this_frame;
} S;

/* -- pipeline ------------------------------------------------------------- */

static bool s_create_pipeline(SafiRenderer *r) {
    SDL_GPUShader *vs = safi_shader_load(r, SAFI_ENGINE_SHADER_DIR,
                                         "gizmo_line", "gizmo_vs",
                                         SAFI_SHADER_STAGE_VERTEX,
                                         0, 1, 0, 0);
    SDL_GPUShader *fs = safi_shader_load(r, SAFI_ENGINE_SHADER_DIR,
                                         "gizmo_line", "gizmo_fs",
                                         SAFI_SHADER_STAGE_FRAGMENT,
                                         0, 0, 0, 0);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(r->device, vs);
        if (fs) SDL_ReleaseGPUShader(r->device, fs);
        SAFI_LOG_ERROR("gizmo: shader load failed");
        return false;
    }

    SDL_GPUVertexBufferDescription vbd = {
        .slot               = 0,
        .pitch              = sizeof(GizmoVertex),
        .input_rate         = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };
    SDL_GPUVertexAttribute attrs[2] = {
        { .buffer_slot = 0, .location = 0,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
          .offset = offsetof(GizmoVertex, pos) },
        { .buffer_slot = 0, .location = 1,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
          .offset = offsetof(GizmoVertex, rgba) },
    };

    SDL_GPUColorTargetDescription color_desc = {
        .format = r->swapchain_format,
        .blend_state = {
            .enable_blend          = true,
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op        = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op        = SDL_GPU_BLENDOP_ADD,
        },
    };

    SDL_GPUGraphicsPipelineCreateInfo pci = {
        .vertex_shader   = vs,
        .fragment_shader = fs,
        .vertex_input_state = {
            .vertex_buffer_descriptions = &vbd,
            .num_vertex_buffers         = 1,
            .vertex_attributes          = attrs,
            .num_vertex_attributes      = 2,
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST,
        .rasterizer_state = {
            .fill_mode  = SDL_GPU_FILLMODE_FILL,
            .cull_mode  = SDL_GPU_CULLMODE_NONE,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        },
        .multisample_state = { .sample_count = SDL_GPU_SAMPLECOUNT_1 },
        .depth_stencil_state = {
            /* Test on, write off — gizmos are hidden behind solid geometry
             * but don't perturb the depth buffer for subsequent passes. */
            .enable_depth_test  = true,
            .enable_depth_write = false,
            .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        },
        .target_info = {
            .color_target_descriptions = &color_desc,
            .num_color_targets         = 1,
            .depth_stencil_format      = r->depth_format,
            .has_depth_stencil_target  = true,
        },
    };
    S.pipeline = SDL_CreateGPUGraphicsPipeline(r->device, &pci);
    SDL_ReleaseGPUShader(r->device, vs);
    SDL_ReleaseGPUShader(r->device, fs);
    if (!S.pipeline) {
        SAFI_LOG_ERROR("gizmo: pipeline create failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool safi_gizmo_system_init(SafiRenderer *r) {
    if (S.initialized) return true;
    if (!r || !r->device) return false;

    S.device = r->device;
    S.window = r->window;

    if (!s_create_pipeline(r)) return false;

    SDL_GPUBufferCreateInfo bi = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size  = GIZMO_VBO_BYTES,
    };
    S.vbo = SDL_CreateGPUBuffer(r->device, &bi);
    if (!S.vbo) {
        SAFI_LOG_ERROR("gizmo: VBO create failed: %s", SDL_GetError());
        return false;
    }

    S.vert_cap = GIZMO_MAX_VERTS;
    S.verts    = (GizmoVertex *)malloc((size_t)S.vert_cap * sizeof(GizmoVertex));
    if (!S.verts) return false;

    S.vert_count                  = 0;
    S.uploaded_verts              = 0;
    S.overflow_logged_this_frame  = false;
    S.initialized                 = true;
    return true;
}

void safi_gizmo_system_destroy(SafiRenderer *r) {
    if (!S.initialized) return;
    if (S.verts) { free(S.verts); S.verts = NULL; }
    if (S.vbo && r && r->device) {
        SDL_ReleaseGPUBuffer(r->device, S.vbo);
        S.vbo = NULL;
    }
    if (S.pipeline && r && r->device) {
        SDL_ReleaseGPUGraphicsPipeline(r->device, S.pipeline);
        S.pipeline = NULL;
    }
    memset(&S, 0, sizeof(S));
}

/* -- enqueue -------------------------------------------------------------- */

static void s_push_vertex(const float pos[3], const float rgba[4]) {
    if (!S.initialized) return;
    if (S.vert_count >= S.vert_cap) {
        if (!S.overflow_logged_this_frame) {
            SAFI_LOG_WARN("gizmo: draw list full at %d verts; further draws "
                          "this frame are dropped", S.vert_cap);
            S.overflow_logged_this_frame = true;
        }
        return;
    }
    GizmoVertex *v = &S.verts[S.vert_count++];
    v->pos[0]  = pos[0];  v->pos[1]  = pos[1];  v->pos[2]  = pos[2];
    v->rgba[0] = rgba[0]; v->rgba[1] = rgba[1];
    v->rgba[2] = rgba[2]; v->rgba[3] = rgba[3];
}

void safi_gizmo_draw_line(const float a[3], const float b[3],
                          const float rgba[4]) {
    s_push_vertex(a, rgba);
    s_push_vertex(b, rgba);
}

void safi_gizmo_draw_box_wire(const float center[3], const float half[3],
                              const float rgba[4]) {
    /* 8 corners, 12 edges. */
    float c[8][3];
    for (int i = 0; i < 8; i++) {
        c[i][0] = center[0] + ((i & 1) ? half[0] : -half[0]);
        c[i][1] = center[1] + ((i & 2) ? half[1] : -half[1]);
        c[i][2] = center[2] + ((i & 4) ? half[2] : -half[2]);
    }
    static const int edges[12][2] = {
        /* bottom (y-) */
        {0,1}, {1,3}, {3,2}, {2,0},
        /* top    (y+) */
        {4,5}, {5,7}, {7,6}, {6,4},
        /* verticals */
        {0,4}, {1,5}, {2,6}, {3,7},
    };
    for (int i = 0; i < 12; i++) {
        safi_gizmo_draw_line(c[edges[i][0]], c[edges[i][1]], rgba);
    }
}

void safi_gizmo_draw_aabb(const float min[3], const float max[3],
                          const float rgba[4]) {
    float center[3] = {
        0.5f * (min[0] + max[0]),
        0.5f * (min[1] + max[1]),
        0.5f * (min[2] + max[2]),
    };
    float half[3] = {
        0.5f * (max[0] - min[0]),
        0.5f * (max[1] - min[1]),
        0.5f * (max[2] - min[2]),
    };
    safi_gizmo_draw_box_wire(center, half, rgba);
}

void safi_gizmo_draw_sphere_wire(const float center[3], float radius,
                                 int segments, const float rgba[4]) {
    if (segments < 4) segments = 4;
    /* Three orthogonal great circles (XY, XZ, YZ). */
    for (int axis = 0; axis < 3; axis++) {
        for (int i = 0; i < segments; i++) {
            float t0 = (float)i       / (float)segments * 2.0f * (float)M_PI;
            float t1 = (float)(i + 1) / (float)segments * 2.0f * (float)M_PI;
            float c0 = cosf(t0), s0 = sinf(t0);
            float c1 = cosf(t1), s1 = sinf(t1);
            float a[3], b[3];
            switch (axis) {
            case 0: /* XY */
                a[0] = center[0] + radius * c0; a[1] = center[1] + radius * s0; a[2] = center[2];
                b[0] = center[0] + radius * c1; b[1] = center[1] + radius * s1; b[2] = center[2];
                break;
            case 1: /* XZ */
                a[0] = center[0] + radius * c0; a[1] = center[1]; a[2] = center[2] + radius * s0;
                b[0] = center[0] + radius * c1; b[1] = center[1]; b[2] = center[2] + radius * s1;
                break;
            default: /* YZ */
                a[0] = center[0]; a[1] = center[1] + radius * c0; a[2] = center[2] + radius * s0;
                b[0] = center[0]; b[1] = center[1] + radius * c1; b[2] = center[2] + radius * s1;
                break;
            }
            safi_gizmo_draw_line(a, b, rgba);
        }
    }
}

/* -- per-frame upload / draw ---------------------------------------------- */

void safi_gizmo_system_upload(SafiRenderer *r) {
    if (!S.initialized || !r || !r->cmd) {
        S.uploaded_verts = 0;
        return;
    }
    S.uploaded_verts = 0;
    if (S.vert_count == 0) return;

    size_t bytes = (size_t)S.vert_count * sizeof(GizmoVertex);
    SDL_GPUTransferBufferCreateInfo tbi = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = (uint32_t)bytes,
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(r->device, &tbi);
    if (!tb) return;
    void *mapped = SDL_MapGPUTransferBuffer(r->device, tb, false);
    memcpy(mapped, S.verts, bytes);
    SDL_UnmapGPUTransferBuffer(r->device, tb);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(r->cmd);
    SDL_GPUTransferBufferLocation src = { .transfer_buffer = tb, .offset = 0 };
    SDL_GPUBufferRegion           dst = {
        .buffer = S.vbo, .offset = 0, .size = (uint32_t)bytes
    };
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(r->device, tb);

    S.uploaded_verts = S.vert_count;
}

void safi_gizmo_system_draw(SafiRenderer *r, const SafiCameraBuffer *cam) {
    if (!S.initialized || !r || !r->pass || !cam) return;
    if (S.uploaded_verts <= 0) {
        /* Still clear the queue so callers can keep enqueuing without
         * us stacking across frames when the frame was dropped. */
        S.vert_count                 = 0;
        S.overflow_logged_this_frame = false;
        return;
    }

    /* view_proj = proj * view (column-major, matches cglm/HLSL convention). */
    mat4 view_m, proj_m, view_proj;
    memcpy(view_m, cam->view, sizeof(view_m));
    memcpy(proj_m, cam->proj, sizeof(proj_m));
    glm_mat4_mul(proj_m, view_m, view_proj);

    SDL_PushGPUVertexUniformData(r->cmd, 0, view_proj, sizeof(view_proj));
    SDL_BindGPUGraphicsPipeline(r->pass, S.pipeline);
    SDL_GPUBufferBinding vbind = { .buffer = S.vbo, .offset = 0 };
    SDL_BindGPUVertexBuffers(r->pass, 0, &vbind, 1);
    SDL_DrawGPUPrimitives(r->pass, (uint32_t)S.uploaded_verts, 1, 0, 0);

    /* End-of-frame reset. */
    S.vert_count                 = 0;
    S.uploaded_verts             = 0;
    S.overflow_logged_this_frame = false;
}
