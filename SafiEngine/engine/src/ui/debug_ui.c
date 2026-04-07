/*
 * Nuklear debug-UI backend for SafiEngine (SDL3 + SDL_gpu, Metal format).
 *
 * Owns:
 *   - a Nuklear context + font atlas baked into a GPU texture
 *   - a graphics pipeline rendering the Nuklear vertex format
 *   - reusable vertex/index GPU buffers sized to SAFI_NK_VBO_BYTES / _IBO_BYTES
 *   - CPU conversion buffers (nk_buffer) for nk_convert
 *
 * Frame shape (matches safi_renderer_*):
 *   safi_renderer_begin_frame         -> cmd + swapchain
 *   safi_debug_ui_begin_frame         -> nk_input_end, ready for widgets
 *   <user widget calls: nk_begin/.../nk_end>
 *   safi_debug_ui_prepare             -> nk_convert + GPU upload (copy pass)
 *   safi_renderer_begin_main_pass     -> main color+depth pass opens
 *   <draw geometry>
 *   safi_debug_ui_render              -> iterate draw commands, scissor+draw
 *   safi_renderer_end_main_pass
 *   safi_renderer_end_frame           -> submit
 *
 * The whole file is C11. Nuklear is a single-header library included here
 * with NK_IMPLEMENTATION defined exactly once in the engine.
 */

#include "safi/ui/debug_ui.h"
#include "safi/core/log.h"
#include "safi/ecs/components.h"
#include "safi/render/shader.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <cglm/cglm.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define NK_IMPLEMENTATION
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include <nuklear.h>

/* -- tuning ---------------------------------------------------------------- */
#define SAFI_NK_VBO_BYTES  (512 * 1024)
#define SAFI_NK_IBO_BYTES  (128 * 1024)

/* -- vertex layout --------------------------------------------------------- */
typedef struct SafiNkVertex {
    float    position[2];
    float    uv[2];
    uint8_t  color[4];
} SafiNkVertex;

/* Nuklear's shader lives in engine/src/ui/shaders/nuklear.hlsl and is
 * compiled to SPIR-V + MSL at build time (see cmake/SafiShaders.cmake).
 * The generated artifacts land in SAFI_ENGINE_SHADER_DIR, which is
 * injected as a compile definition from engine/CMakeLists.txt. */

/* -- module state ---------------------------------------------------------- */
static struct {
    bool initialized;

    struct nk_context            ctx;
    struct nk_font_atlas         atlas;
    struct nk_draw_null_texture  null_tex;
    struct nk_buffer             cmds;   /* nk_context command buffer storage */
    struct nk_buffer             verts;  /* CPU vertex scratch */
    struct nk_buffer             idxs;   /* CPU index  scratch */

    SDL_Window              *window;
    SDL_GPUDevice           *device;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUSampler          *sampler;
    SDL_GPUTexture          *font_tex;
    SDL_GPUBuffer           *vbo;
    SDL_GPUBuffer           *ibo;

    uint8_t *vbo_cpu;
    uint8_t *ibo_cpu;

    ecs_entity_t selected_entity;
    float        dpi_scale;
} S;

/* -- helpers --------------------------------------------------------------- */

static bool s_upload_font_atlas(SafiRenderer *r,
                                const void   *pixels,
                                int           width,
                                int           height) {
    SDL_GPUTextureCreateInfo ti = {
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .width                = (uint32_t)width,
        .height               = (uint32_t)height,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
        .sample_count         = SDL_GPU_SAMPLECOUNT_1,
        .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER,
    };
    S.font_tex = SDL_CreateGPUTexture(r->device, &ti);
    if (!S.font_tex) {
        SAFI_LOG_ERROR("Nuklear: font atlas texture create failed: %s", SDL_GetError());
        return false;
    }

    uint32_t bytes = (uint32_t)(width * height * 4);
    SDL_GPUTransferBufferCreateInfo tbi = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = bytes,
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(r->device, &tbi);
    void *mapped = SDL_MapGPUTransferBuffer(r->device, tb, false);
    memcpy(mapped, pixels, bytes);
    SDL_UnmapGPUTransferBuffer(r->device, tb);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(r->device);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = {
        .transfer_buffer = tb,
        .offset          = 0,
        .pixels_per_row  = (uint32_t)width,
        .rows_per_layer  = (uint32_t)height,
    };
    SDL_GPUTextureRegion dst = {
        .texture = S.font_tex,
        .w = (uint32_t)width, .h = (uint32_t)height, .d = 1,
    };
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(r->device, tb);
    return true;
}

static bool s_create_pipeline(SafiRenderer *r) {
    SDL_GPUShader *vs = safi_shader_load(r, SAFI_ENGINE_SHADER_DIR,
                                         "nuklear", "nk_vs",
                                         SAFI_SHADER_STAGE_VERTEX,
                                         0, 1, 0, 0);
    SDL_GPUShader *fs = safi_shader_load(r, SAFI_ENGINE_SHADER_DIR,
                                         "nuklear", "nk_fs",
                                         SAFI_SHADER_STAGE_FRAGMENT,
                                         1, 0, 0, 0);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(r->device, vs);
        if (fs) SDL_ReleaseGPUShader(r->device, fs);
        return false;
    }

    SDL_GPUVertexBufferDescription vbd = {
        .slot               = 0,
        .pitch              = sizeof(SafiNkVertex),
        .input_rate         = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };
    SDL_GPUVertexAttribute attrs[3] = {
        { .buffer_slot = 0, .location = 0,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
          .offset = offsetof(SafiNkVertex, position) },
        { .buffer_slot = 0, .location = 1,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
          .offset = offsetof(SafiNkVertex, uv) },
        { .buffer_slot = 0, .location = 2,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
          .offset = offsetof(SafiNkVertex, color) },
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
            .num_vertex_attributes      = 3,
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {
            .fill_mode  = SDL_GPU_FILLMODE_FILL,
            .cull_mode  = SDL_GPU_CULLMODE_NONE,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        },
        .multisample_state = { .sample_count = SDL_GPU_SAMPLECOUNT_1 },
        .depth_stencil_state = {
            .enable_depth_test  = false,
            .enable_depth_write = false,
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
        SAFI_LOG_ERROR("Nuklear: pipeline create failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUSamplerCreateInfo si = {
        .min_filter     = SDL_GPU_FILTER_LINEAR,
        .mag_filter     = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    };
    S.sampler = SDL_CreateGPUSampler(r->device, &si);
    return S.sampler != NULL;
}

static bool s_create_gpu_buffers(SafiRenderer *r) {
    SDL_GPUBufferCreateInfo vbi = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size  = SAFI_NK_VBO_BYTES,
    };
    S.vbo = SDL_CreateGPUBuffer(r->device, &vbi);
    SDL_GPUBufferCreateInfo ibi = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size  = SAFI_NK_IBO_BYTES,
    };
    S.ibo = SDL_CreateGPUBuffer(r->device, &ibi);
    S.vbo_cpu = (uint8_t *)malloc(SAFI_NK_VBO_BYTES);
    S.ibo_cpu = (uint8_t *)malloc(SAFI_NK_IBO_BYTES);
    return S.vbo && S.ibo && S.vbo_cpu && S.ibo_cpu;
}

/* -- public API ------------------------------------------------------------ */

bool safi_debug_ui_init(SafiRenderer *r) {
    memset(&S, 0, sizeof(S));
    S.window = r->window;
    S.device = r->device;

    if (!nk_init_default(&S.ctx, NULL)) {
        SAFI_LOG_ERROR("Nuklear: nk_init_default failed");
        return false;
    }

    S.dpi_scale = r->dpi_scale;

    /* Bake the default font into an RGBA32 atlas, scaled for HiDPI. */
    nk_font_atlas_init_default(&S.atlas);
    nk_font_atlas_begin(&S.atlas);
    struct nk_font *font = nk_font_atlas_add_default(&S.atlas,
                                                      14.0f * S.dpi_scale, NULL);
    int atlas_w, atlas_h;
    const void *pixels = nk_font_atlas_bake(&S.atlas, &atlas_w, &atlas_h, NK_FONT_ATLAS_RGBA32);
    if (!pixels || !s_upload_font_atlas(r, pixels, atlas_w, atlas_h)) return false;

    nk_font_atlas_end(&S.atlas, nk_handle_ptr(S.font_tex), &S.null_tex);
    if (S.atlas.default_font) font = S.atlas.default_font;
    /* The atlas glyphs are baked at pixel size (14 * dpi_scale) for sharp
     * rendering, but Nuklear layout must use the logical point size so
     * widgets don't appear oversized. */
    font->handle.height = 14.0f;
    nk_style_set_font(&S.ctx, &font->handle);

    if (!s_create_pipeline(r))       return false;
    if (!s_create_gpu_buffers(r))    return false;

    nk_buffer_init_default(&S.cmds);
    nk_buffer_init_default(&S.verts);
    nk_buffer_init_default(&S.idxs);

    /* Open the input collection window so the very first frame's events
     * (processed before safi_debug_ui_begin_frame runs) land in an active
     * begin/end bracket. */
    nk_input_begin(&S.ctx);

    /* Enable SDL text input so SDL_EVENT_TEXT_INPUT events are generated.
     * Without this, Nuklear property widgets cannot accept typed numbers. */
    SDL_StartTextInput(r->window);

    S.initialized = true;
    return true;
}

void safi_debug_ui_shutdown(SafiRenderer *r) {
    if (!S.initialized) return;

    nk_buffer_free(&S.cmds);
    nk_buffer_free(&S.verts);
    nk_buffer_free(&S.idxs);
    nk_font_atlas_clear(&S.atlas);
    nk_free(&S.ctx);

    if (S.vbo)      SDL_ReleaseGPUBuffer(r->device, S.vbo);
    if (S.ibo)      SDL_ReleaseGPUBuffer(r->device, S.ibo);
    if (S.font_tex) SDL_ReleaseGPUTexture(r->device, S.font_tex);
    if (S.sampler)  SDL_ReleaseGPUSampler(r->device, S.sampler);
    if (S.pipeline) SDL_ReleaseGPUGraphicsPipeline(r->device, S.pipeline);

    free(S.vbo_cpu);
    free(S.ibo_cpu);

    memset(&S, 0, sizeof(S));
}

/* Translate an SDL event into Nuklear input. Called per event by the engine
 * input system — nk_input_begin / nk_input_end bracketing happens in
 * safi_debug_ui_begin_frame. */
void safi_debug_ui_process_event(const void *sdl_event) {
    if (!S.initialized) return;
    const SDL_Event *e = (const SDL_Event *)sdl_event;
    struct nk_context *ctx = &S.ctx;

    switch (e->type) {
    case SDL_EVENT_MOUSE_MOTION:
        nk_input_motion(ctx, (int)e->motion.x, (int)e->motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        bool down = (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        int x = (int)e->button.x, y = (int)e->button.y;
        enum nk_buttons b = NK_BUTTON_LEFT;
        switch (e->button.button) {
            case SDL_BUTTON_LEFT:   b = NK_BUTTON_LEFT; break;
            case SDL_BUTTON_MIDDLE: b = NK_BUTTON_MIDDLE; break;
            case SDL_BUTTON_RIGHT:  b = NK_BUTTON_RIGHT; break;
            default: return;
        }
        nk_input_button(ctx, b, x, y, down);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        nk_input_scroll(ctx, nk_vec2(e->wheel.x, e->wheel.y));
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        bool down = (e->type == SDL_EVENT_KEY_DOWN);
        SDL_Scancode sc = e->key.scancode;
        switch (sc) {
            case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT:
                nk_input_key(ctx, NK_KEY_SHIFT, down); break;
            case SDL_SCANCODE_LCTRL:  case SDL_SCANCODE_RCTRL:
                nk_input_key(ctx, NK_KEY_CTRL, down); break;
            case SDL_SCANCODE_DELETE:
                nk_input_key(ctx, NK_KEY_DEL, down); break;
            case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
                nk_input_key(ctx, NK_KEY_ENTER, down); break;
            case SDL_SCANCODE_TAB:
                nk_input_key(ctx, NK_KEY_TAB, down); break;
            case SDL_SCANCODE_BACKSPACE:
                nk_input_key(ctx, NK_KEY_BACKSPACE, down); break;
            case SDL_SCANCODE_LEFT:
                nk_input_key(ctx, NK_KEY_LEFT, down); break;
            case SDL_SCANCODE_RIGHT:
                nk_input_key(ctx, NK_KEY_RIGHT, down); break;
            case SDL_SCANCODE_UP:
                nk_input_key(ctx, NK_KEY_UP, down); break;
            case SDL_SCANCODE_DOWN:
                nk_input_key(ctx, NK_KEY_DOWN, down); break;
            default: break;
        }
        break;
    }
    case SDL_EVENT_TEXT_INPUT: {
        const char *p = e->text.text;
        while (*p) nk_input_char(ctx, *p++);
        break;
    }
    default: break;
    }
}

void safi_debug_ui_begin_frame(SafiRenderer *r) {
    (void)r;
    if (!S.initialized) return;
    /* Finalize events gathered since the last render and leave the input
     * state frozen so widgets observe mouse clicks, deltas, etc. The next
     * nk_input_begin happens at the end of safi_debug_ui_render, after the
     * frame is done. Calling nk_input_begin here would reset clicked flags
     * before widgets could read them. */
    nk_input_end(&S.ctx);
}

/* Convert widgets → vertices/indices, upload to GPU. Runs a copy pass; must
 * be called BEFORE safi_renderer_begin_main_pass. */
void safi_debug_ui_prepare(SafiRenderer *r) {
    if (!S.initialized || !r->cmd) return;
    /* Input was already ended in safi_debug_ui_begin_frame. Do not end it
     * again here — that would leave nuklear outside a valid input bracket. */

    struct nk_convert_config cfg = {0};
    static const struct nk_draw_vertex_layout_element layout[] = {
        { NK_VERTEX_POSITION, NK_FORMAT_FLOAT,    NK_OFFSETOF(SafiNkVertex, position) },
        { NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,    NK_OFFSETOF(SafiNkVertex, uv) },
        { NK_VERTEX_COLOR,    NK_FORMAT_R8G8B8A8, NK_OFFSETOF(SafiNkVertex, color) },
        { NK_VERTEX_LAYOUT_END }
    };
    cfg.vertex_layout        = layout;
    cfg.vertex_size          = sizeof(SafiNkVertex);
    cfg.vertex_alignment     = NK_ALIGNOF(SafiNkVertex);
    cfg.tex_null             = S.null_tex;
    cfg.circle_segment_count = 22;
    cfg.curve_segment_count  = 22;
    cfg.arc_segment_count    = 22;
    cfg.global_alpha         = 1.0f;
    cfg.shape_AA             = NK_ANTI_ALIASING_ON;
    cfg.line_AA              = NK_ANTI_ALIASING_ON;

    nk_buffer_clear(&S.verts);
    nk_buffer_clear(&S.idxs);

    /* Wrap the fixed CPU staging buffers so nk_convert writes straight into
     * them — no second memcpy. */
    struct nk_buffer v_stage, i_stage;
    nk_buffer_init_fixed(&v_stage, S.vbo_cpu, SAFI_NK_VBO_BYTES);
    nk_buffer_init_fixed(&i_stage, S.ibo_cpu, SAFI_NK_IBO_BYTES);

    nk_flags flags = nk_convert(&S.ctx, &S.cmds, &v_stage, &i_stage, &cfg);
    if (flags & NK_CONVERT_VERTEX_BUFFER_FULL)  SAFI_LOG_WARN("Nuklear: vertex buffer full");
    if (flags & NK_CONVERT_ELEMENT_BUFFER_FULL) SAFI_LOG_WARN("Nuklear: element buffer full");

    size_t v_bytes = nk_buffer_total(&v_stage);
    size_t i_bytes = nk_buffer_total(&i_stage);
    if (v_bytes == 0 || i_bytes == 0) return;

    /* Upload to GPU through a transient transfer buffer. */
    SDL_GPUTransferBufferCreateInfo tbi = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = (uint32_t)(v_bytes + i_bytes),
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(r->device, &tbi);
    uint8_t *mapped = (uint8_t *)SDL_MapGPUTransferBuffer(r->device, tb, false);
    memcpy(mapped,           S.vbo_cpu, v_bytes);
    memcpy(mapped + v_bytes, S.ibo_cpu, i_bytes);
    SDL_UnmapGPUTransferBuffer(r->device, tb);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(r->cmd);
    SDL_GPUTransferBufferLocation v_src = { .transfer_buffer = tb, .offset = 0 };
    SDL_GPUBufferRegion           v_dst = { .buffer = S.vbo, .offset = 0, .size = (uint32_t)v_bytes };
    SDL_UploadToGPUBuffer(copy, &v_src, &v_dst, false);
    SDL_GPUTransferBufferLocation i_src = { .transfer_buffer = tb, .offset = (uint32_t)v_bytes };
    SDL_GPUBufferRegion           i_dst = { .buffer = S.ibo, .offset = 0, .size = (uint32_t)i_bytes };
    SDL_UploadToGPUBuffer(copy, &i_src, &i_dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_ReleaseGPUTransferBuffer(r->device, tb);
}

void safi_debug_ui_render(SafiRenderer *r) {
    if (!S.initialized || !r->pass) return;

    /* Push viewport size uniform (inv_half_viewport = 2/w, 2/h).
     * Nuklear vertex positions are in logical points, so divide by logical
     * (not pixel) dimensions to map to NDC. */
    float lw = (float)r->swapchain_w / S.dpi_scale;
    float lh = (float)r->swapchain_h / S.dpi_scale;
    float ubo[2] = { 2.0f / lw, 2.0f / lh };
    SDL_PushGPUVertexUniformData(r->cmd, 0, ubo, sizeof(ubo));

    SDL_BindGPUGraphicsPipeline(r->pass, S.pipeline);

    SDL_GPUBufferBinding vbind = { .buffer = S.vbo, .offset = 0 };
    SDL_BindGPUVertexBuffers(r->pass, 0, &vbind, 1);
    SDL_GPUBufferBinding ibind = { .buffer = S.ibo, .offset = 0 };
    SDL_BindGPUIndexBuffer(r->pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    uint32_t index_offset = 0;
    const struct nk_draw_command *cmd;
    nk_draw_foreach(cmd, &S.ctx, &S.cmds) {
        if (!cmd->elem_count) continue;

        /* Scissor in pixels — Nuklear's clip_rect is in logical points,
         * scale to pixel coordinates for the GPU. */
        float sc = S.dpi_scale;
        SDL_Rect scissor = {
            .x = (int)((cmd->clip_rect.x > 0 ? cmd->clip_rect.x : 0) * sc),
            .y = (int)((cmd->clip_rect.y > 0 ? cmd->clip_rect.y : 0) * sc),
            .w = (int)(cmd->clip_rect.w * sc),
            .h = (int)(cmd->clip_rect.h * sc),
        };
        if (scissor.w < 0) scissor.w = 0;
        if (scissor.h < 0) scissor.h = 0;
        /* Clamp to swapchain to avoid driver asserts on oversized rects. */
        if ((uint32_t)scissor.x > r->swapchain_w) scissor.x = (int)r->swapchain_w;
        if ((uint32_t)scissor.y > r->swapchain_h) scissor.y = (int)r->swapchain_h;
        if (scissor.x + scissor.w > (int)r->swapchain_w) scissor.w = (int)r->swapchain_w - scissor.x;
        if (scissor.y + scissor.h > (int)r->swapchain_h) scissor.h = (int)r->swapchain_h - scissor.y;
        SDL_SetGPUScissor(r->pass, &scissor);

        SDL_GPUTexture *tex = (SDL_GPUTexture *)cmd->texture.ptr;
        if (!tex) tex = S.font_tex;
        SDL_GPUTextureSamplerBinding sbind = { .texture = tex, .sampler = S.sampler };
        SDL_BindGPUFragmentSamplers(r->pass, 0, &sbind, 1);

        SDL_DrawGPUIndexedPrimitives(r->pass, cmd->elem_count, 1, index_offset, 0, 0);
        index_offset += cmd->elem_count;
    }

    nk_clear(&S.ctx);
    nk_buffer_clear(&S.cmds);
    nk_input_begin(&S.ctx);  /* reopen input for next frame */
}

/* Expose the context to examples that want to add widgets. */
struct nk_context *safi_debug_ui_context(void) {
    return S.initialized ? &S.ctx : NULL;
}

bool safi_debug_ui_wants_input(void) {
    return S.initialized && nk_item_is_any_active(&S.ctx);
}

ecs_entity_t safi_debug_ui_selected_entity(void) {
    return S.selected_entity;
}

void safi_debug_ui_select_entity(ecs_entity_t e) {
    S.selected_entity = e;
}

void safi_debug_ui_draw_panels(SafiRenderer *r, ecs_world_t *world) {
    if (!S.initialized) return;
    struct nk_context *ctx = &S.ctx;
    char buf[128];

    /* ---- Scene Hierarchy (left side) --------------------------------- */
    if (nk_begin(ctx, "Scene",
                 nk_rect(20, 20, 200, 400),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE |
                 NK_WINDOW_SCALABLE | NK_WINDOW_TITLE)) {
        ecs_query_t *q = ecs_query(world, {
            .terms = {{ .id = ecs_id(SafiName) }},
            .cache_kind = EcsQueryCacheNone,
        });
        ecs_iter_t qit = ecs_query_iter(world, q);
        while (ecs_query_next(&qit)) {
            const SafiName *names = ecs_field(&qit, SafiName, 0);
            for (int i = 0; i < qit.count; i++) {
                nk_layout_row_dynamic(ctx, 22, 1);
                nk_bool is_sel = (qit.entities[i] == S.selected_entity);
                if (nk_selectable_label(ctx, names[i].value,
                                        NK_TEXT_LEFT, &is_sel)) {
                    S.selected_entity = qit.entities[i];
                }
            }
        }
        ecs_query_fini(q);
    }
    nk_end(ctx);

    /* ---- Inspector (right side) -------------------------------------- */
    float insp_x = (float)r->swapchain_w / S.dpi_scale - 320.0f;
    if (insp_x < 240.0f) insp_x = 240.0f;

    if (nk_begin(ctx, "Inspector",
                 nk_rect(insp_x, 20, 300, 520),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE |
                 NK_WINDOW_SCALABLE | NK_WINDOW_TITLE)) {

        if (!S.selected_entity) {
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "No entity selected", NK_TEXT_LEFT);
            goto inspector_end;
        }

        /* Entity name header */
        const SafiName *name = ecs_get(world, S.selected_entity, SafiName);
        if (name) {
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, name->value, NK_TEXT_LEFT);
        }

        /* ---- SafiTransform ------------------------------------------- */
        if (ecs_has(world, S.selected_entity, SafiTransform)) {
            SafiTransform *xf = ecs_get_mut(world, S.selected_entity,
                                            SafiTransform);
            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "Position", NK_TEXT_LEFT);
            nk_property_float(ctx, "pos x", -100.0f,
                              &xf->position[0], 100.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "pos y", -100.0f,
                              &xf->position[1], 100.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "pos z", -100.0f,
                              &xf->position[2], 100.0f, 0.1f, 0.01f);

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
            nk_property_float(ctx, "rot x", -180.0f,
                              &rot_deg[0], 180.0f, 1.0f, 0.5f);
            nk_property_float(ctx, "rot y", -180.0f,
                              &rot_deg[1], 180.0f, 1.0f, 0.5f);
            nk_property_float(ctx, "rot z", -180.0f,
                              &rot_deg[2], 180.0f, 1.0f, 0.5f);

            euler_rad[0] = glm_rad(rot_deg[0]);
            euler_rad[1] = glm_rad(rot_deg[1]);
            euler_rad[2] = glm_rad(rot_deg[2]);
            glm_euler_xyz(euler_rad, rot_mat);
            glm_mat4_quat(rot_mat, xf->rotation);

            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "Scale", NK_TEXT_LEFT);
            nk_property_float(ctx, "scale x", 0.01f,
                              &xf->scale[0], 100.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "scale y", 0.01f,
                              &xf->scale[1], 100.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "scale z", 0.01f,
                              &xf->scale[2], 100.0f, 0.1f, 0.01f);
        }

        /* ---- SafiCamera ---------------------------------------------- */
        if (ecs_has(world, S.selected_entity, SafiCamera)) {
            SafiCamera *cam = ecs_get_mut(world, S.selected_entity,
                                          SafiCamera);
            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "Camera", NK_TEXT_LEFT);

            float fov_deg = glm_deg(cam->fov_y_radians);
            nk_property_float(ctx, "FOV", 1.0f, &fov_deg, 179.0f,
                              1.0f, 0.5f);
            cam->fov_y_radians = glm_rad(fov_deg);

            nk_property_float(ctx, "Near", 0.001f,
                              &cam->z_near, 100.0f, 0.01f, 0.001f);
            nk_property_float(ctx, "Far", 1.0f,
                              &cam->z_far, 10000.0f, 1.0f, 0.5f);

            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "Target", NK_TEXT_LEFT);
            nk_property_float(ctx, "target x", -100.0f,
                              &cam->target[0], 100.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "target y", -100.0f,
                              &cam->target[1], 100.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "target z", -100.0f,
                              &cam->target[2], 100.0f, 0.1f, 0.01f);
        }

        /* ---- SafiMeshRenderer ---------------------------------------- */
        if (ecs_has(world, S.selected_entity, SafiMeshRenderer)) {
            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "MeshRenderer", NK_TEXT_LEFT);

            const SafiMeshRenderer *mr = ecs_get(world, S.selected_entity,
                                                  SafiMeshRenderer);
            snprintf(buf, sizeof(buf), "Mesh: %s",
                     mr->mesh ? "assigned" : "none");
            nk_label(ctx, buf, NK_TEXT_LEFT);
            snprintf(buf, sizeof(buf), "Material: %s",
                     mr->material ? "assigned" : "none");
            nk_label(ctx, buf, NK_TEXT_LEFT);
        }

        /* ---- SafiSpin ------------------------------------------------ */
        if (ecs_has(world, S.selected_entity, SafiSpin)) {
            SafiSpin *spin = ecs_get_mut(world, S.selected_entity, SafiSpin);
            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "Spin", NK_TEXT_LEFT);
            nk_property_float(ctx, "speed", -20.0f,
                              &spin->speed, 20.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "axis x", -1.0f,
                              &spin->axis[0], 1.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "axis y", -1.0f,
                              &spin->axis[1], 1.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "axis z", -1.0f,
                              &spin->axis[2], 1.0f, 0.1f, 0.01f);
        }

        /* ---- SafiDirectionalLight ------------------------------------ */
        if (ecs_has(world, S.selected_entity, SafiDirectionalLight)) {
            SafiDirectionalLight *dl = ecs_get_mut(world, S.selected_entity,
                                                    SafiDirectionalLight);
            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "Directional Light", NK_TEXT_LEFT);
            nk_property_float(ctx, "dir x", -1.0f,
                              &dl->direction[0], 1.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "dir y", -1.0f,
                              &dl->direction[1], 1.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "dir z", -1.0f,
                              &dl->direction[2], 1.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "color r", 0.0f,
                              &dl->color[0], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "color g", 0.0f,
                              &dl->color[1], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "color b", 0.0f,
                              &dl->color[2], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "intensity", 0.0f,
                              &dl->intensity, 10.0f, 0.1f, 0.01f);
        }

        /* ---- SafiPointLight ------------------------------------------ */
        if (ecs_has(world, S.selected_entity, SafiPointLight)) {
            SafiPointLight *pl = ecs_get_mut(world, S.selected_entity,
                                              SafiPointLight);
            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "Point Light", NK_TEXT_LEFT);
            nk_property_float(ctx, "color r", 0.0f,
                              &pl->color[0], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "color g", 0.0f,
                              &pl->color[1], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "color b", 0.0f,
                              &pl->color[2], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "intensity", 0.0f,
                              &pl->intensity, 10.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "range", 0.01f,
                              &pl->range, 1000.0f, 1.0f, 0.1f);
        }

        /* ---- SafiSpotLight ------------------------------------------- */
        if (ecs_has(world, S.selected_entity, SafiSpotLight)) {
            SafiSpotLight *sl = ecs_get_mut(world, S.selected_entity,
                                             SafiSpotLight);
            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "Spot Light", NK_TEXT_LEFT);
            nk_property_float(ctx, "color r", 0.0f,
                              &sl->color[0], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "color g", 0.0f,
                              &sl->color[1], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "color b", 0.0f,
                              &sl->color[2], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "intensity", 0.0f,
                              &sl->intensity, 10.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "range", 0.01f,
                              &sl->range, 1000.0f, 1.0f, 0.1f);
            nk_property_float(ctx, "inner angle", 0.0f,
                              &sl->inner_angle, 1.0f, 0.01f, 0.005f);
            nk_property_float(ctx, "outer angle", 0.0f,
                              &sl->outer_angle, 1.0f, 0.01f, 0.005f);
        }

        /* ---- SafiRectLight ------------------------------------------- */
        if (ecs_has(world, S.selected_entity, SafiRectLight)) {
            SafiRectLight *rl = ecs_get_mut(world, S.selected_entity,
                                             SafiRectLight);
            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "Rect Light", NK_TEXT_LEFT);
            nk_property_float(ctx, "color r", 0.0f,
                              &rl->color[0], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "color g", 0.0f,
                              &rl->color[1], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "color b", 0.0f,
                              &rl->color[2], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "intensity", 0.0f,
                              &rl->intensity, 10.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "width", 0.01f,
                              &rl->width, 100.0f, 0.1f, 0.01f);
            nk_property_float(ctx, "height", 0.01f,
                              &rl->height, 100.0f, 0.1f, 0.01f);
        }

        /* ---- SafiSkyLight -------------------------------------------- */
        if (ecs_has(world, S.selected_entity, SafiSkyLight)) {
            SafiSkyLight *sk = ecs_get_mut(world, S.selected_entity,
                                            SafiSkyLight);
            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "Sky Light", NK_TEXT_LEFT);
            nk_property_float(ctx, "color r", 0.0f,
                              &sk->color[0], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "color g", 0.0f,
                              &sk->color[1], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "color b", 0.0f,
                              &sk->color[2], 1.0f, 0.05f, 0.01f);
            nk_property_float(ctx, "intensity", 0.0f,
                              &sk->intensity, 10.0f, 0.1f, 0.01f);
        }
    }
inspector_end:
    nk_end(ctx);
}
