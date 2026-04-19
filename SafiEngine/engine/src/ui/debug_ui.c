/*
 * MicroUI debug-UI backend for SafiEngine (SDL3 + SDL_gpu).
 *
 * Owns:
 *   - a MicroUI context
 *   - a stb_truetype font atlas baked into a GPU texture
 *   - a batched-quad graphics pipeline (same vertex format as before)
 *   - reusable vertex/index GPU buffers
 *   - CPU staging arrays for batching quads per frame
 *
 * Frame shape (matches safi_renderer_*):
 *   safi_renderer_begin_frame         -> cmd + swapchain
 *   safi_debug_ui_begin_frame         -> mu_begin
 *   <user widget calls: mu_begin_window/.../mu_end_window>
 *   safi_debug_ui_prepare             -> mu_end + batch quads + GPU upload (copy pass)
 *   safi_renderer_begin_main_pass     -> main color+depth pass opens
 *   <draw geometry>
 *   safi_debug_ui_render              -> iterate draw commands, scissor+draw
 *   safi_renderer_end_main_pass
 *   safi_renderer_end_frame           -> submit
 *
 * The whole file is C11. MicroUI is a small library (~1100 SLOC) included
 * via its header; the .c is compiled separately by CMake.
 */

#include "safi/ui/debug_ui.h"
#include "safi/core/log.h"
#include "safi/ecs/components.h"
#include "safi/editor/editor_state.h"
#include "safi/editor/editor_toolbar.h"
#include "safi/render/assets.h"
#include "safi/physics/physics.h"
#include "safi/render/shader.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <cglm/cglm.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <microui.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

/* -- tuning ---------------------------------------------------------------- */
#define MU_VBO_BYTES   (512 * 1024)
#define MU_IBO_BYTES   (128 * 1024)
#define MU_MAX_VERTS   (MU_VBO_BYTES / (int)sizeof(MuVertex))
#define MU_MAX_INDICES (MU_IBO_BYTES / (int)sizeof(uint16_t))

#define FONT_ATLAS_W   512
#define FONT_ATLAS_H   512
#define FONT_FIRST_CHAR 32
#define FONT_NUM_CHARS  95  /* ASCII 32..126 */
#define FONT_SIZE_PT    13.0f

/* Maximum draw commands we can record per frame. Each scissor change or
 * texture change produces a new draw command. */
#define MAX_DRAW_CMDS  256

/* -- vertex layout --------------------------------------------------------- */
typedef struct MuVertex {
    float    position[2];
    float    uv[2];
    uint8_t  color[4];
} MuVertex;

/* -- recorded draw command for the render pass ----------------------------- */
typedef struct MuDrawCmd {
    SDL_Rect scissor;
    uint32_t index_offset;
    uint32_t index_count;
} MuDrawCmd;

/* -- module state ---------------------------------------------------------- */
static struct {
    bool initialized;

    mu_Context ctx;

    /* Font atlas (stb_truetype) */
    SDL_GPUTexture  *font_tex;
    stbtt_bakedchar  cdata[FONT_NUM_CHARS];
    float            font_height;   /* logical height reported to MicroUI */
    float            dpi_scale;

    /* GPU resources */
    SDL_Window              *window;
    SDL_GPUDevice           *device;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUSampler          *sampler;
    SDL_GPUBuffer           *vbo;
    SDL_GPUBuffer           *ibo;

    /* CPU batch buffers */
    MuVertex *verts;
    uint16_t *indices;
    int       vert_count;
    int       index_count;

    /* Draw command recording */
    MuDrawCmd draw_cmds[MAX_DRAW_CMDS];
    int       draw_cmd_count;
    SDL_Rect  current_scissor;
    uint32_t  batch_index_start;

    /* White pixel UV for solid-color rectangles (top-left corner of atlas) */
    float white_u;
    float white_v;

    /* Double-click detection for number widgets */
    mu_Id    last_click_id;      /* widget ID of last click */
    uint64_t last_click_time;    /* SDL ticks of last click */

    /* Inspector/Scene panel selection now lives on SafiEditorState so the
     * gizmo system and any other editor tool can read the same value —
     * see `safi_editor_{get,set}_selected` in editor/editor_state.c. */

    /* Dropdown state — at most one dropdown is open at any time. The
     * enum widget records an open-request here; the popup is rendered by
     * s_draw_open_dropdown() after the Inspector window closes.
     *
     * Selections are NOT written through a saved pointer (callers often
     * pass stack-local ints that would dangle across frames). Instead,
     * the popup records `result_index` against `result_for_id`; the enum
     * widget reads-and-clears the pending result on the next frame. */
    struct {
        mu_Id              open_id;       /* 0 = closed */
        const char *const *names;
        int                count;
        mu_Rect            anchor;        /* button rect, for drop-below positioning */

        bool               has_result;
        mu_Id              result_for_id;
        int                result_index;
    } dropdown;
} S;

/* -- font callbacks for MicroUI ------------------------------------------- */

static int s_text_width(mu_Font font, const char *str, int len) {
    (void)font;
    if (len == -1) len = (int)strlen(str);
    float w = 0;
    for (int i = 0; i < len; i++) {
        int c = (unsigned char)str[i] - FONT_FIRST_CHAR;
        if (c >= 0 && c < FONT_NUM_CHARS) {
            w += S.cdata[c].xadvance;
        }
    }
    return (int)(w / S.dpi_scale);
}

static int s_text_height(mu_Font font) {
    (void)font;
    return (int)S.font_height;
}

/* -- helpers --------------------------------------------------------------- */

static bool s_load_and_bake_font(SafiRenderer *r) {
    /* Read TTF file */
    FILE *f = fopen(SAFI_ENGINE_FONT_PATH, "rb");
    if (!f) {
        SAFI_LOG_ERROR("MicroUI: cannot open font: %s", SAFI_ENGINE_FONT_PATH);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *ttf_data = (unsigned char *)malloc((size_t)fsize);
    if (!ttf_data) { fclose(f); return false; }
    fread(ttf_data, 1, (size_t)fsize, f);
    fclose(f);

    /* Bake at pixel size for HiDPI */
    float pixel_size = FONT_SIZE_PT * S.dpi_scale;
    unsigned char *alpha_bitmap = (unsigned char *)malloc(FONT_ATLAS_W * FONT_ATLAS_H);
    if (!alpha_bitmap) { free(ttf_data); return false; }

    int bake_ret = stbtt_BakeFontBitmap(ttf_data, 0, pixel_size,
                                         alpha_bitmap, FONT_ATLAS_W, FONT_ATLAS_H,
                                         FONT_FIRST_CHAR, FONT_NUM_CHARS, S.cdata);
    free(ttf_data);
    if (bake_ret <= 0) {
        SAFI_LOG_WARN("MicroUI: font bake returned %d (may have missing glyphs)", bake_ret);
    }

    /* Convert alpha-only bitmap to RGBA (white text, alpha from bitmap).
     * Reserve pixel (0,0) as a solid white pixel for rectangle drawing. */
    unsigned char *rgba = (unsigned char *)malloc(FONT_ATLAS_W * FONT_ATLAS_H * 4);
    if (!rgba) { free(alpha_bitmap); return false; }
    for (int i = 0; i < FONT_ATLAS_W * FONT_ATLAS_H; i++) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = alpha_bitmap[i];
    }
    /* Force pixel (0,0) to solid white for rectangle UVs */
    rgba[0] = rgba[1] = rgba[2] = rgba[3] = 255;
    free(alpha_bitmap);

    S.white_u = 0.5f / (float)FONT_ATLAS_W;
    S.white_v = 0.5f / (float)FONT_ATLAS_H;

    /* Upload to GPU texture */
    SDL_GPUTextureCreateInfo ti = {
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .width                = FONT_ATLAS_W,
        .height               = FONT_ATLAS_H,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
        .sample_count         = SDL_GPU_SAMPLECOUNT_1,
        .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER,
    };
    S.font_tex = SDL_CreateGPUTexture(r->device, &ti);
    if (!S.font_tex) {
        SAFI_LOG_ERROR("MicroUI: font atlas texture create failed: %s", SDL_GetError());
        free(rgba);
        return false;
    }

    uint32_t bytes = FONT_ATLAS_W * FONT_ATLAS_H * 4;
    SDL_GPUTransferBufferCreateInfo tbi = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = bytes,
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(r->device, &tbi);
    void *mapped = SDL_MapGPUTransferBuffer(r->device, tb, false);
    memcpy(mapped, rgba, bytes);
    SDL_UnmapGPUTransferBuffer(r->device, tb);
    free(rgba);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(r->device);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = {
        .transfer_buffer = tb,
        .offset          = 0,
        .pixels_per_row  = FONT_ATLAS_W,
        .rows_per_layer  = FONT_ATLAS_H,
    };
    SDL_GPUTextureRegion dst = {
        .texture = S.font_tex,
        .w = FONT_ATLAS_W, .h = FONT_ATLAS_H, .d = 1,
    };
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(r->device, tb);

    S.font_height = FONT_SIZE_PT;
    return true;
}

static bool s_create_pipeline(SafiRenderer *r) {
    SDL_GPUShader *vs = safi_shader_load(r, SAFI_ENGINE_SHADER_DIR,
                                         "microui", "mu_vs",
                                         SAFI_SHADER_STAGE_VERTEX,
                                         0, 1, 0, 0);
    SDL_GPUShader *fs = safi_shader_load(r, SAFI_ENGINE_SHADER_DIR,
                                         "microui", "mu_fs",
                                         SAFI_SHADER_STAGE_FRAGMENT,
                                         1, 0, 0, 0);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(r->device, vs);
        if (fs) SDL_ReleaseGPUShader(r->device, fs);
        return false;
    }

    SDL_GPUVertexBufferDescription vbd = {
        .slot               = 0,
        .pitch              = sizeof(MuVertex),
        .input_rate         = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };
    SDL_GPUVertexAttribute attrs[3] = {
        { .buffer_slot = 0, .location = 0,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
          .offset = offsetof(MuVertex, position) },
        { .buffer_slot = 0, .location = 1,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
          .offset = offsetof(MuVertex, uv) },
        { .buffer_slot = 0, .location = 2,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
          .offset = offsetof(MuVertex, color) },
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
        SAFI_LOG_ERROR("MicroUI: pipeline create failed: %s", SDL_GetError());
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
        .size  = MU_VBO_BYTES,
    };
    S.vbo = SDL_CreateGPUBuffer(r->device, &vbi);
    SDL_GPUBufferCreateInfo ibi = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size  = MU_IBO_BYTES,
    };
    S.ibo = SDL_CreateGPUBuffer(r->device, &ibi);
    S.verts   = (MuVertex *)malloc(MU_VBO_BYTES);
    S.indices = (uint16_t *)malloc(MU_IBO_BYTES);
    return S.vbo && S.ibo && S.verts && S.indices;
}

/* -- quad batching --------------------------------------------------------- */

static void s_push_quad(float x, float y, float w, float h,
                        float u0, float v0, float u1, float v1,
                        mu_Color color) {
    if (S.vert_count + 4 > MU_MAX_VERTS || S.index_count + 6 > MU_MAX_INDICES)
        return;

    uint16_t vi = (uint16_t)S.vert_count;
    MuVertex *v = &S.verts[S.vert_count];
    uint8_t c[4] = { color.r, color.g, color.b, color.a };

    v[0] = (MuVertex){ {x,     y    }, {u0, v0}, {c[0], c[1], c[2], c[3]} };
    v[1] = (MuVertex){ {x + w, y    }, {u1, v0}, {c[0], c[1], c[2], c[3]} };
    v[2] = (MuVertex){ {x + w, y + h}, {u1, v1}, {c[0], c[1], c[2], c[3]} };
    v[3] = (MuVertex){ {x,     y + h}, {u0, v1}, {c[0], c[1], c[2], c[3]} };
    S.vert_count += 4;

    uint16_t *idx = &S.indices[S.index_count];
    idx[0] = vi; idx[1] = vi + 1; idx[2] = vi + 2;
    idx[3] = vi; idx[4] = vi + 2; idx[5] = vi + 3;
    S.index_count += 6;
}

static void s_push_rect(mu_Rect rect, mu_Color color) {
    s_push_quad((float)rect.x, (float)rect.y,
                (float)rect.w, (float)rect.h,
                S.white_u, S.white_v, S.white_u, S.white_v,
                color);
}

static void s_push_text(const char *str, mu_Vec2 pos, mu_Color color) {
    float x = (float)pos.x * S.dpi_scale;
    float y = (float)pos.y * S.dpi_scale;

    while (*str) {
        int c = (unsigned char)*str - FONT_FIRST_CHAR;
        if (c >= 0 && c < FONT_NUM_CHARS) {
            stbtt_bakedchar *b = &S.cdata[c];
            float x0 = x + b->xoff;
            float y0 = y + b->yoff + FONT_SIZE_PT * S.dpi_scale;
            float x1 = x0 + (b->x1 - b->x0);
            float y1 = y0 + (b->y1 - b->y0);
            float u0 = (float)b->x0 / FONT_ATLAS_W;
            float v0 = (float)b->y0 / FONT_ATLAS_H;
            float u1 = (float)b->x1 / FONT_ATLAS_W;
            float v1 = (float)b->y1 / FONT_ATLAS_H;

            /* Positions are in pixel space (scaled), convert back to logical
             * for the vertex shader which works in logical coords. */
            s_push_quad(x0 / S.dpi_scale, y0 / S.dpi_scale,
                        (x1 - x0) / S.dpi_scale, (y1 - y0) / S.dpi_scale,
                        u0, v0, u1, v1, color);

            x += b->xadvance;
        }
        str++;
    }
}

static void s_push_icon(int id, mu_Rect rect, mu_Color color) {
    /* MicroUI icons: MU_ICON_CLOSE (1), MU_ICON_CHECK (2),
     * MU_ICON_COLLAPSED (3), MU_ICON_EXPANDED (4).
     * Render as simple geometric shapes using rectangles. */
    int cx = rect.x + rect.w / 2;
    int cy = rect.y + rect.h / 2;

    switch (id) {
    case MU_ICON_CLOSE: {
        /* Small X: two overlapping rectangles */
        int sz = 6;
        s_push_rect(mu_rect(cx - sz/2, cy - 1, sz, 2), color);
        s_push_rect(mu_rect(cx - 1, cy - sz/2, 2, sz), color);
        break;
    }
    case MU_ICON_CHECK: {
        /* Simple checkmark: small filled square */
        int sz = 8;
        s_push_rect(mu_rect(cx - sz/2, cy - sz/2, sz, sz), color);
        break;
    }
    case MU_ICON_COLLAPSED: {
        /* Right-pointing triangle approximation: stacked rects */
        s_push_rect(mu_rect(cx - 2, cy - 4, 2, 8), color);
        s_push_rect(mu_rect(cx,     cy - 2, 2, 4), color);
        s_push_rect(mu_rect(cx + 2, cy - 1, 2, 2), color);
        break;
    }
    case MU_ICON_EXPANDED: {
        /* Down-pointing triangle approximation: stacked rects */
        s_push_rect(mu_rect(cx - 4, cy - 2, 8, 2), color);
        s_push_rect(mu_rect(cx - 2, cy,     4, 2), color);
        s_push_rect(mu_rect(cx - 1, cy + 2, 2, 2), color);
        break;
    }
    default:
        break;
    }
}

static void s_record_draw_cmd(void) {
    uint32_t count = (uint32_t)S.index_count - S.batch_index_start;
    if (count == 0) return;
    if (S.draw_cmd_count >= MAX_DRAW_CMDS) return;

    S.draw_cmds[S.draw_cmd_count++] = (MuDrawCmd){
        .scissor      = S.current_scissor,
        .index_offset = S.batch_index_start,
        .index_count  = count,
    };
    S.batch_index_start = (uint32_t)S.index_count;
}

/* -- public API ------------------------------------------------------------ */

bool safi_debug_ui_init(SafiRenderer *r) {
    memset(&S, 0, sizeof(S));
    S.window = r->window;
    S.device = r->device;
    S.dpi_scale = r->dpi_scale;

    mu_init(&S.ctx);
    S.ctx.text_width  = s_text_width;
    S.ctx.text_height = s_text_height;

    if (!s_load_and_bake_font(r))    return false;
    if (!s_create_pipeline(r))       return false;
    if (!s_create_gpu_buffers(r))    return false;

    SDL_StartTextInput(r->window);

    S.initialized = true;
    return true;
}

void safi_debug_ui_shutdown(SafiRenderer *r) {
    if (!S.initialized) return;

    if (S.vbo)      SDL_ReleaseGPUBuffer(r->device, S.vbo);
    if (S.ibo)      SDL_ReleaseGPUBuffer(r->device, S.ibo);
    if (S.font_tex) SDL_ReleaseGPUTexture(r->device, S.font_tex);
    if (S.sampler)  SDL_ReleaseGPUSampler(r->device, S.sampler);
    if (S.pipeline) SDL_ReleaseGPUGraphicsPipeline(r->device, S.pipeline);

    free(S.verts);
    free(S.indices);

    memset(&S, 0, sizeof(S));
}

void safi_debug_ui_process_event(const void *sdl_event) {
    if (!S.initialized) return;
    const SDL_Event *e = (const SDL_Event *)sdl_event;
    mu_Context *ctx = &S.ctx;

    switch (e->type) {
    case SDL_EVENT_MOUSE_MOTION:
        mu_input_mousemove(ctx, (int)e->motion.x, (int)e->motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        int x = (int)e->button.x, y = (int)e->button.y;
        int btn = 0;
        switch (e->button.button) {
            case SDL_BUTTON_LEFT:   btn = MU_MOUSE_LEFT; break;
            case SDL_BUTTON_MIDDLE: btn = MU_MOUSE_MIDDLE; break;
            case SDL_BUTTON_RIGHT:  btn = MU_MOUSE_RIGHT; break;
            default: return;
        }
        if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            mu_input_mousedown(ctx, x, y, btn);
        else
            mu_input_mouseup(ctx, x, y, btn);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        mu_input_scroll(ctx, (int)(e->wheel.x * -30),
                              (int)(e->wheel.y * -30));
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        int down = (e->type == SDL_EVENT_KEY_DOWN);
        SDL_Scancode sc = e->key.scancode;
        int key = 0;
        switch (sc) {
            case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT:
                key = MU_KEY_SHIFT; break;
            case SDL_SCANCODE_LCTRL:  case SDL_SCANCODE_RCTRL:
                key = MU_KEY_CTRL; break;
            case SDL_SCANCODE_LALT:   case SDL_SCANCODE_RALT:
                key = MU_KEY_ALT; break;
            case SDL_SCANCODE_BACKSPACE:
                key = MU_KEY_BACKSPACE; break;
            case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
                key = MU_KEY_RETURN; break;
            default: break;
        }
        if (key) {
            if (down) mu_input_keydown(ctx, key);
            else      mu_input_keyup(ctx, key);
        }
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        mu_input_text(ctx, e->text.text);
        break;
    default:
        break;
    }
}

void safi_debug_ui_begin_frame(SafiRenderer *r) {
    (void)r;
    if (!S.initialized) return;
    mu_begin(&S.ctx);
}

void safi_debug_ui_prepare(SafiRenderer *r) {
    if (!S.initialized || !r->cmd) return;

    mu_end(&S.ctx);

    /* Reset batch state */
    S.vert_count       = 0;
    S.index_count      = 0;
    S.draw_cmd_count   = 0;
    S.batch_index_start = 0;

    /* Default scissor covers the whole pixel viewport */
    S.current_scissor = (SDL_Rect){ 0, 0, (int)r->swapchain_w, (int)r->swapchain_h };

    /* Iterate MicroUI commands and batch quads */
    mu_Command *cmd = NULL;
    while (mu_next_command(&S.ctx, &cmd)) {
        switch (cmd->type) {
        case MU_COMMAND_CLIP: {
            /* Flush current batch before changing scissor */
            s_record_draw_cmd();
            /* Convert logical clip rect to pixel coords */
            float sc = S.dpi_scale;
            mu_Rect cr = cmd->clip.rect;
            SDL_Rect scissor = {
                .x = (int)(cr.x > 0 ? cr.x * sc : 0),
                .y = (int)(cr.y > 0 ? cr.y * sc : 0),
                .w = (int)(cr.w * sc),
                .h = (int)(cr.h * sc),
            };
            if (scissor.w < 0) scissor.w = 0;
            if (scissor.h < 0) scissor.h = 0;
            if ((uint32_t)scissor.x > r->swapchain_w) scissor.x = (int)r->swapchain_w;
            if ((uint32_t)scissor.y > r->swapchain_h) scissor.y = (int)r->swapchain_h;
            if (scissor.x + scissor.w > (int)r->swapchain_w) scissor.w = (int)r->swapchain_w - scissor.x;
            if (scissor.y + scissor.h > (int)r->swapchain_h) scissor.h = (int)r->swapchain_h - scissor.y;
            S.current_scissor = scissor;
            break;
        }
        case MU_COMMAND_RECT:
            s_push_rect(cmd->rect.rect, cmd->rect.color);
            break;
        case MU_COMMAND_TEXT:
            s_push_text(cmd->text.str, cmd->text.pos, cmd->text.color);
            break;
        case MU_COMMAND_ICON:
            s_push_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color);
            break;
        }
    }
    /* Flush final batch */
    s_record_draw_cmd();

    /* Upload to GPU */
    size_t v_bytes = (size_t)S.vert_count * sizeof(MuVertex);
    size_t i_bytes = (size_t)S.index_count * sizeof(uint16_t);
    if (v_bytes == 0 || i_bytes == 0) return;

    SDL_GPUTransferBufferCreateInfo tbi = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = (uint32_t)(v_bytes + i_bytes),
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(r->device, &tbi);
    uint8_t *mapped = (uint8_t *)SDL_MapGPUTransferBuffer(r->device, tb, false);
    memcpy(mapped,           S.verts,   v_bytes);
    memcpy(mapped + v_bytes, S.indices, i_bytes);
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
    if (!S.initialized || !r->pass || S.draw_cmd_count == 0) return;

    /* Push viewport size uniform. Vertex positions are in logical points,
     * divide by logical dimensions to map to NDC. */
    float lw = (float)r->swapchain_w / S.dpi_scale;
    float lh = (float)r->swapchain_h / S.dpi_scale;
    float ubo[2] = { 2.0f / lw, 2.0f / lh };
    SDL_PushGPUVertexUniformData(r->cmd, 0, ubo, sizeof(ubo));

    SDL_BindGPUGraphicsPipeline(r->pass, S.pipeline);

    SDL_GPUBufferBinding vbind = { .buffer = S.vbo, .offset = 0 };
    SDL_BindGPUVertexBuffers(r->pass, 0, &vbind, 1);
    SDL_GPUBufferBinding ibind = { .buffer = S.ibo, .offset = 0 };
    SDL_BindGPUIndexBuffer(r->pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    SDL_GPUTextureSamplerBinding sbind = { .texture = S.font_tex, .sampler = S.sampler };
    SDL_BindGPUFragmentSamplers(r->pass, 0, &sbind, 1);

    for (int i = 0; i < S.draw_cmd_count; i++) {
        MuDrawCmd *dc = &S.draw_cmds[i];
        SDL_SetGPUScissor(r->pass, &dc->scissor);
        SDL_DrawGPUIndexedPrimitives(r->pass, dc->index_count, 1,
                                     dc->index_offset, 0, 0);
    }
}

/* -- context access -------------------------------------------------------- */

mu_Context *safi_debug_ui_context(void) {
    return S.initialized ? &S.ctx : NULL;
}

bool safi_debug_ui_wants_input(void) {
    return S.initialized && (S.ctx.focus != 0);
}

bool safi_debug_ui_mouse_over_viewport(void) {
    /* hover_root is MicroUI's "which root window is the mouse over" field —
     * NULL means the cursor is in empty viewport space. */
    return !S.initialized || S.ctx.hover_root == NULL;
}

/* -- built-in panels ------------------------------------------------------- */

/* Single number cell: drag to change, double-click for text input.
 * After mu_number_ex runs (which sets hover/focus), we check for a
 * double-click and trigger MicroUI's built-in text edit mode for the
 * next frame.
 *
 * These widget functions are exposed publicly via inspector_widgets.h
 * so the component registry's per-component draw callbacks can use
 * them without duplicating the dropdown/double-click plumbing. */
void safi_inspector_number_cell(mu_Context *ctx, float *val, float step) {
    mu_Id id = mu_get_id(ctx, &val, sizeof(val));

    mu_number_ex(ctx, val, step, "%.2f", MU_OPT_ALIGNCENTER);

    /* Detect double-click on this widget (hover is set by mu_number_ex) */
    if ((ctx->mouse_pressed & MU_MOUSE_LEFT) &&
        (ctx->hover == id || ctx->focus == id)) {
        uint64_t now = SDL_GetTicksNS();
        if (S.last_click_id == id &&
            (now - S.last_click_time) < 400000000ULL) {
            /* Double-click — activate MicroUI's built-in text edit */
            ctx->number_edit = id;
            sprintf(ctx->number_edit_buf, MU_REAL_FMT, *val);
            S.last_click_id   = 0;
            S.last_click_time = 0;
        } else {
            S.last_click_id   = id;
            S.last_click_time = now;
        }
    }
}

/* Label on the left + single number on the right. */
void safi_inspector_property_float(mu_Context *ctx, const char *label,
                              float *val, float step) {
    mu_layout_row(ctx, 2, (int[]){ 80, -1 }, 0);
    mu_label(ctx, label);
    safi_inspector_number_cell(ctx, val, step);
}

/* Label on the left + three X/Y/Z numbers in one row.
 * We compute equal widths for the 3 cells from the container width. */
void safi_inspector_property_vec3(mu_Context *ctx, const char *label,
                             float *xyz, float step) {
    int avail = mu_get_current_container(ctx)->body.w -
                ctx->style->padding * 2;
    int label_w = 80;
    int cell_w  = (avail - label_w - ctx->style->spacing * 3) / 3;
    mu_layout_row(ctx, 4, (int[]){ label_w, cell_w, cell_w, cell_w }, 0);
    mu_label(ctx, label);
    safi_inspector_number_cell(ctx, &xyz[0], step);
    safi_inspector_number_cell(ctx, &xyz[1], step);
    safi_inspector_number_cell(ctx, &xyz[2], step);
}

/* Label on the left + four R/G/B/A numbers in one row. Mirrors
 * safi_inspector_property_vec3 — the inline 4-cell layout keeps color tweaking visually
 * distinct from direction/size vec3 fields. */
void safi_inspector_property_color_rgba(mu_Context *ctx, const char *label,
                                   float rgba[4], float step) {
    int avail = mu_get_current_container(ctx)->body.w -
                ctx->style->padding * 2;
    int label_w = 80;
    int cell_w  = (avail - label_w - ctx->style->spacing * 4) / 4;
    mu_layout_row(ctx, 5, (int[]){ label_w, cell_w, cell_w, cell_w, cell_w }, 0);
    mu_label(ctx, label);
    safi_inspector_number_cell(ctx, &rgba[0], step);
    safi_inspector_number_cell(ctx, &rgba[1], step);
    safi_inspector_number_cell(ctx, &rgba[2], step);
    safi_inspector_number_cell(ctx, &rgba[3], step);
}

/* Label + single-line text input bound to a char buffer. Wraps mu_textbox_ex
 * so the caller can bind a SafiPrimitive.texture_path or similar. */
void safi_inspector_property_string(mu_Context *ctx, const char *label,
                               char *buf, int cap) {
    mu_layout_row(ctx, 2, (int[]){ 80, -1 }, 0);
    mu_label(ctx, label);
    mu_textbox_ex(ctx, buf, cap, 0);
}

/* Label + checkbox bound to a bool. MicroUI's checkbox takes int*, so we
 * mirror the bool through a local int. */
void safi_inspector_property_bool(mu_Context *ctx, const char *label, bool *val) {
    mu_layout_row(ctx, 2, (int[]){ 80, -1 }, 0);
    mu_label(ctx, label);
    int state = *val ? 1 : 0;
    mu_checkbox(ctx, "", &state);
    *val = (state != 0);
}

/* Label + dropdown button. Clicking the button records a "please open"
 * request into module state; the actual popup is drawn later by
 * s_draw_open_dropdown() after the Inspector window has ended, so the
 * popup isn't clipped to the Inspector's body and doesn't fight its
 * layout. */
void safi_inspector_property_enum(mu_Context *ctx, const char *label, int *value,
                            const char *const *names, int count) {
    mu_layout_row(ctx, 2, (int[]){ 80, -1 }, 0);
    mu_label(ctx, label);

    mu_Id id = mu_get_id(ctx, &value, sizeof(value));

    /* Consume a pending selection from a previous frame. */
    if (S.dropdown.has_result && S.dropdown.result_for_id == id) {
        *value = S.dropdown.result_index;
        S.dropdown.has_result    = false;
        S.dropdown.result_for_id = 0;
    }

    const char *current =
        (*value >= 0 && *value < count) ? names[*value] : "?";

    if (mu_button(ctx, current)) {
        S.dropdown.open_id = id;
        S.dropdown.names   = names;
        S.dropdown.count   = count;
        S.dropdown.anchor  = ctx->last_rect;
    }
}

/* Renders the currently-open dropdown (if any) as a floating popup. Called
 * once per frame, after the Inspector window's mu_end_window.
 *
 * We deliberately do NOT use mu_begin_popup(): its hardcoded flags
 * include MU_OPT_AUTOSIZE (opening frame is 1x1 → invisible on first click)
 * and MU_OPT_NOSCROLL (long lists overflow). Calling mu_begin_window_ex
 * directly with MU_OPT_POPUP keeps microui's click-outside-to-close
 * behavior while letting us pre-position and allow scrolling. */
static void s_draw_open_dropdown(mu_Context *ctx) {
    if (!S.dropdown.open_id) return;

    const char *name = "safi_dropdown";
    mu_Container *cnt = mu_get_container(ctx, name);

    /* First frame of this opening: position directly below the anchor,
     * match its width, cap height. */
    if (!cnt->open) {
        int row_h = 24;
        int pad   = 12;
        int desired_h = S.dropdown.count * row_h + pad;
        if (desired_h > 200) desired_h = 200;

        cnt->rect.x = S.dropdown.anchor.x;
        cnt->rect.y = S.dropdown.anchor.y + S.dropdown.anchor.h;
        cnt->rect.w = S.dropdown.anchor.w;
        cnt->rect.h = desired_h;
        cnt->open   = 1;
        mu_bring_to_front(ctx, cnt);
        /* Critical: keep hover_root on the popup so MU_OPT_POPUP's
         * click-outside check doesn't kill it on the same frame. */
        ctx->hover_root = ctx->next_hover_root = cnt;
    }

    int opt = MU_OPT_POPUP | MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_CLOSED;
    if (mu_begin_window_ex(ctx, name, cnt->rect, opt)) {
        mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
        for (int i = 0; i < S.dropdown.count; i++) {
            if (mu_button(ctx, S.dropdown.names[i])) {
                S.dropdown.result_for_id = S.dropdown.open_id;
                S.dropdown.result_index  = i;
                S.dropdown.has_result    = true;
                S.dropdown.open_id       = 0;
                cnt->open = 0;
            }
        }
        mu_end_window(ctx);
    } else {
        /* click-outside closed it, or container was recycled */
        S.dropdown.open_id = 0;
    }
}

/* ---- Scene tree expand/collapse state ---------------------------------- */
#define SCENE_NODE_CAP 256
static bool s_scene_expanded[SCENE_NODE_CAP];
static bool s_scene_expanded_init = false;

static bool scene_is_expanded(ecs_entity_t e) {
    return s_scene_expanded[(uint32_t)e % SCENE_NODE_CAP];
}
static void scene_toggle_expanded(ecs_entity_t e) {
    s_scene_expanded[(uint32_t)e % SCENE_NODE_CAP] ^= 1;
}

/* Recursively draw an entity node in the Scene hierarchy panel.
 *
 * Parent entities (those with EcsChildOf children) get a small arrow
 * toggle to collapse/expand their subtree. Clicking the arrow
 * toggles expansion; clicking the name button selects the entity.
 * Leaf entities render as a plain button with no arrow. */
static void draw_scene_entity(mu_Context *ctx, ecs_world_t *world,
                              ecs_entity_t e, int depth) {
    const SafiName *name = ecs_get(world, e, SafiName);
    if (!name) return;

    if (!s_scene_expanded_init) {
        memset(s_scene_expanded, 1, sizeof(s_scene_expanded));
        s_scene_expanded_init = true;
    }

    /* Collect named children into a stack buffer. */
    #define MAX_SCENE_CHILDREN 64
    ecs_entity_t children[MAX_SCENE_CHILDREN];
    int child_count = 0;
    {
        ecs_iter_t cit = ecs_children(world, e);
        while (ecs_children_next(&cit) && child_count < MAX_SCENE_CHILDREN) {
            for (int i = 0; i < cit.count && child_count < MAX_SCENE_CHILDREN; i++) {
                if (ecs_get(world, cit.entities[i], SafiName)) {
                    children[child_count++] = cit.entities[i];
                }
            }
        }
    }
    bool has_children = (child_count > 0);
    int indent = depth * 14;
    int arrow_w = has_children ? 18 : 0;

    /* Build the layout row: [indent spacer?] [arrow?] [name button] */
    if (indent > 0 && has_children) {
        mu_layout_row(ctx, 3, (int[]){ indent, arrow_w, -1 }, 22);
        mu_layout_next(ctx); /* skip indent spacer */
    } else if (indent > 0) {
        mu_layout_row(ctx, 2, (int[]){ indent, -1 }, 22);
        mu_layout_next(ctx); /* skip indent spacer */
    } else if (has_children) {
        mu_layout_row(ctx, 2, (int[]){ arrow_w, -1 }, 22);
    } else {
        mu_layout_row(ctx, 1, (int[]){ -1 }, 22);
    }

    /* Arrow toggle (only for parent entities). */
    if (has_children) {
        mu_push_id(ctx, &e, sizeof(e));
        if (mu_button(ctx, scene_is_expanded(e) ? "v" : ">")) {
            scene_toggle_expanded(e);
        }
        mu_pop_id(ctx);
    }

    /* Entity name button with selection highlight. */
    bool is_sel = (e == safi_editor_get_selected(world));
    if (is_sel) {
        ctx->style->colors[MU_COLOR_BUTTON]      = mu_color(60, 60, 140, 255);
        ctx->style->colors[MU_COLOR_BUTTONHOVER] = mu_color(70, 70, 160, 255);
        ctx->style->colors[MU_COLOR_BUTTONFOCUS] = mu_color(80, 80, 180, 255);
    }
    if (mu_button(ctx, name->value)) {
        safi_editor_set_selected(world, e);
    }
    if (is_sel) {
        ctx->style->colors[MU_COLOR_BUTTON]      = mu_color(75, 75, 75, 255);
        ctx->style->colors[MU_COLOR_BUTTONHOVER] = mu_color(95, 95, 95, 255);
        ctx->style->colors[MU_COLOR_BUTTONFOCUS] = mu_color(115, 115, 115, 255);
    }

    /* Recurse into children if expanded. */
    if (has_children && scene_is_expanded(e)) {
        for (int c = 0; c < child_count; c++) {
            draw_scene_entity(ctx, world, children[c], depth + 1);
        }
    }
    #undef MAX_SCENE_CHILDREN
}

void safi_debug_ui_draw_panels(SafiRenderer *r, ecs_world_t *world) {
    if (!S.initialized) return;
    mu_Context *ctx = &S.ctx;
    char buf[128];

    /* Toolbar at the top — MicroUI logical pixels, so divide the swapchain
     * by dpi_scale. The toolbar strip is 36 px tall; panels below start at
     * y=52 to leave a small gap. */
    int logical_w = (int)((float)r->swapchain_w / S.dpi_scale);
    safi_editor_toolbar_draw(ctx, world, logical_w);

    /* ---- Scene Hierarchy (left side) --------------------------------- */
    if (mu_begin_window(ctx, "Scene", mu_rect(20, 52, 200, 400))) {
        /* Walk the SafiName query, but only draw ROOT entities here —
         * children are picked up via draw_scene_entity's recursion so
         * they appear indented under their parent. */
        ecs_query_t *q = ecs_query(world, {
            .terms = {{ .id = ecs_id(SafiName) }},
            .cache_kind = EcsQueryCacheNone,
        });
        ecs_iter_t qit = ecs_query_iter(world, q);
        while (ecs_query_next(&qit)) {
            for (int i = 0; i < qit.count; i++) {
                ecs_entity_t e = qit.entities[i];
                if (ecs_get_target(world, e, EcsChildOf, 0) == 0) {
                    draw_scene_entity(ctx, world, e, 0);
                }
            }
        }
        ecs_query_fini(q);
        mu_end_window(ctx);
    }

    /* ---- Inspector (right side) -------------------------------------- */
    float insp_x = (float)r->swapchain_w / S.dpi_scale - 320.0f;
    if (insp_x < 240.0f) insp_x = 240.0f;

    if (mu_begin_window(ctx, "Inspector",
                        mu_rect((int)insp_x, 52, 300, 520))) {

        /* Drop a stale selection (e.g. after scene_load or scene_clear has
         * deleted-and-recreated entities). Otherwise ecs_get asserts on
         * the dead id. */
        ecs_entity_t sel = safi_editor_get_selected(world);
        if (sel && !ecs_is_alive(world, sel)) {
            safi_editor_set_selected(world, 0);
            sel = 0;
        }

        if (!sel) {
            mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
            mu_label(ctx, "No entity selected");
            mu_end_window(ctx);
            return;
        }

        /* Entity name header */
        const SafiName *name = ecs_get(world, sel, SafiName);
        if (name) {
            mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
            mu_label(ctx, name->value);
        }

        /* ---- SafiTransform ------------------------------------------- */
        if (ecs_has(world, sel, SafiTransform)) {
            SafiTransform *xf = ecs_get_mut(world, sel,
                                            SafiTransform);
            if (mu_header_ex(ctx, "Transform", MU_OPT_EXPANDED)) {
                safi_inspector_property_vec3(ctx, "Position", xf->position, 0.1f);

                mat4 rot_mat;
                vec3 euler_rad;
                glm_quat_mat4(xf->rotation, rot_mat);
                glm_euler_angles(rot_mat, euler_rad);
                float rot_deg[3] = {
                    glm_deg(euler_rad[0]),
                    glm_deg(euler_rad[1]),
                    glm_deg(euler_rad[2]),
                };

                safi_inspector_property_vec3(ctx, "Rotation", rot_deg, 1.0f);

                euler_rad[0] = glm_rad(rot_deg[0]);
                euler_rad[1] = glm_rad(rot_deg[1]);
                euler_rad[2] = glm_rad(rot_deg[2]);
                glm_euler_xyz(euler_rad, rot_mat);
                glm_mat4_quat(rot_mat, xf->rotation);

                safi_inspector_property_vec3(ctx, "Scale", xf->scale, 0.1f);
            }
        }

        /* ---- SafiCamera ---------------------------------------------- */
        if (ecs_has(world, sel, SafiCamera)) {
            SafiCamera *cam = ecs_get_mut(world, sel,
                                          SafiCamera);
            if (mu_header_ex(ctx, "Camera", MU_OPT_EXPANDED)) {
                float fov_deg = glm_deg(cam->fov_y_radians);
                safi_inspector_property_float(ctx, "FOV", &fov_deg, 1.0f);
                cam->fov_y_radians = glm_rad(fov_deg);

                safi_inspector_property_float(ctx, "Near", &cam->z_near, 0.01f);
                safi_inspector_property_float(ctx, "Far",  &cam->z_far, 1.0f);

                safi_inspector_property_vec3(ctx, "Target", cam->target, 0.1f);
            }
        }

        /* ---- SafiMeshRenderer ---------------------------------------- */
        if (ecs_has(world, sel, SafiMeshRenderer)) {
            SafiMeshRenderer *mr = ecs_get_mut(world, sel,
                                               SafiMeshRenderer);
            if (mu_header_ex(ctx, "MeshRenderer", MU_OPT_EXPANDED)) {
                mu_layout_row(ctx, 1, (int[]){ -1 }, 0);
                const char *mpath = safi_assets_model_path(mr->model);
                snprintf(buf, sizeof(buf), "Model: %s",
                         mr->model.id ? (mpath[0] ? mpath : "<code>") : "none");
                mu_label(ctx, buf);
                safi_inspector_property_bool(ctx, "Visible", &mr->visible);
            }
        }

        /* ---- SafiPrimitive ------------------------------------------- */
        if (ecs_has(world, sel, SafiPrimitive)) {
            SafiPrimitive *pr = ecs_get_mut(world, sel,
                                             SafiPrimitive);
            if (mu_header_ex(ctx, "Primitive", MU_OPT_EXPANDED)) {
                static const char *const shapes[] = {
                    "Plane", "Box", "Sphere", "Capsule",
                };
                int shape_i = (int)pr->shape;
                safi_inspector_property_enum(ctx, "Shape", &shape_i, shapes, 4);
                pr->shape = (SafiPrimitiveShape)shape_i;

                switch (pr->shape) {
                case SAFI_PRIMITIVE_PLANE:
                    safi_inspector_property_float(ctx, "Size", &pr->dims.plane.size, 0.1f);
                    break;
                case SAFI_PRIMITIVE_BOX:
                    safi_inspector_property_vec3(ctx, "HalfExtents",
                                    pr->dims.box.half_extents, 0.1f);
                    break;
                case SAFI_PRIMITIVE_SPHERE: {
                    safi_inspector_property_float(ctx, "Radius",
                                     &pr->dims.sphere.radius, 0.1f);
                    float seg = (float)pr->dims.sphere.segments;
                    float rng = (float)pr->dims.sphere.rings;
                    safi_inspector_property_float(ctx, "Segments", &seg, 1.0f);
                    safi_inspector_property_float(ctx, "Rings",    &rng, 1.0f);
                    if (seg < 3) seg = 3;
                    if (rng < 2) rng = 2;
                    pr->dims.sphere.segments = (int)seg;
                    pr->dims.sphere.rings    = (int)rng;
                    break;
                }
                case SAFI_PRIMITIVE_CAPSULE: {
                    safi_inspector_property_float(ctx, "Radius",
                                     &pr->dims.capsule.radius, 0.1f);
                    safi_inspector_property_float(ctx, "Height",
                                     &pr->dims.capsule.height, 0.1f);
                    float seg = (float)pr->dims.capsule.segments;
                    float rng = (float)pr->dims.capsule.rings;
                    safi_inspector_property_float(ctx, "Segments", &seg, 1.0f);
                    safi_inspector_property_float(ctx, "Rings",    &rng, 1.0f);
                    if (seg < 3) seg = 3;
                    if (rng < 2) rng = 2;
                    pr->dims.capsule.segments = (int)seg;
                    pr->dims.capsule.rings    = (int)rng;
                    break;
                }
                }

                safi_inspector_property_color_rgba(ctx, "Color", pr->color, 0.05f);
                safi_inspector_property_string(ctx, "Texture",
                                  pr->texture_path,
                                  (int)sizeof(pr->texture_path));
            }
        }

        /* ---- SafiSpin ------------------------------------------------ */
        if (ecs_has(world, sel, SafiSpin)) {
            SafiSpin *spin = ecs_get_mut(world, sel, SafiSpin);
            if (mu_header_ex(ctx, "Spin", MU_OPT_EXPANDED)) {
                safi_inspector_property_float(ctx, "speed", &spin->speed, 0.1f);
                safi_inspector_property_vec3(ctx, "Axis", spin->axis, 0.1f);
            }
        }

        /* ---- SafiRigidBody ------------------------------------------- */
        if (ecs_has(world, sel, SafiRigidBody)) {
            SafiRigidBody *rb = ecs_get_mut(world, sel,
                                            SafiRigidBody);
            if (mu_header_ex(ctx, "RigidBody", MU_OPT_EXPANDED)) {
                static const char *const body_types[] = {
                    "Static", "Dynamic", "Kinematic",
                };
                int type_i = (int)rb->type;
                safi_inspector_property_enum(ctx, "Type", &type_i, body_types, 3);
                rb->type = (SafiBodyType)type_i;

                safi_inspector_property_float(ctx, "Mass",        &rb->mass,        0.1f);
                safi_inspector_property_float(ctx, "Friction",    &rb->friction,    0.05f);
                safi_inspector_property_float(ctx, "Restitution", &rb->restitution, 0.05f);
            }
        }

        /* ---- SafiCollider -------------------------------------------- */
        if (ecs_has(world, sel, SafiCollider)) {
            SafiCollider *col = ecs_get_mut(world, sel,
                                            SafiCollider);
            if (mu_header_ex(ctx, "Collider", MU_OPT_EXPANDED)) {
                static const char *const shapes[] = { "Box", "Sphere" };
                int shape_i = (int)col->shape;
                safi_inspector_property_enum(ctx, "Shape", &shape_i, shapes, 2);
                col->shape = (SafiColliderShape)shape_i;

                switch (col->shape) {
                case SAFI_COLLIDER_BOX:
                    safi_inspector_property_vec3(ctx, "HalfExtents",
                                    col->box.half_extents, 0.1f);
                    break;
                case SAFI_COLLIDER_SPHERE:
                    safi_inspector_property_float(ctx, "Radius",
                                     &col->sphere.radius, 0.1f);
                    break;
                }
            }
        }

        /* ---- SafiDirectionalLight ------------------------------------ */
        if (ecs_has(world, sel, SafiDirectionalLight)) {
            SafiDirectionalLight *dl = ecs_get_mut(world, sel,
                                                    SafiDirectionalLight);
            if (mu_header_ex(ctx, "Directional Light", MU_OPT_EXPANDED)) {
                safi_inspector_property_vec3(ctx, "Direction", dl->direction, 0.1f);
                safi_inspector_property_vec3(ctx, "Color",     dl->color, 0.05f);
                safi_inspector_property_float(ctx, "intensity", &dl->intensity, 0.1f);
            }
        }

        /* ---- SafiPointLight ------------------------------------------ */
        if (ecs_has(world, sel, SafiPointLight)) {
            SafiPointLight *pl = ecs_get_mut(world, sel,
                                              SafiPointLight);
            if (mu_header_ex(ctx, "Point Light", MU_OPT_EXPANDED)) {
                safi_inspector_property_vec3(ctx, "Color", pl->color, 0.05f);
                safi_inspector_property_float(ctx, "intensity", &pl->intensity, 0.1f);
                safi_inspector_property_float(ctx, "range",     &pl->range, 1.0f);
            }
        }

        /* ---- SafiSpotLight ------------------------------------------- */
        if (ecs_has(world, sel, SafiSpotLight)) {
            SafiSpotLight *sl = ecs_get_mut(world, sel,
                                             SafiSpotLight);
            if (mu_header_ex(ctx, "Spot Light", MU_OPT_EXPANDED)) {
                safi_inspector_property_vec3(ctx, "Color", sl->color, 0.05f);
                safi_inspector_property_float(ctx, "intensity",   &sl->intensity, 0.1f);
                safi_inspector_property_float(ctx, "range",       &sl->range, 1.0f);
                safi_inspector_property_float(ctx, "inner angle", &sl->inner_angle, 0.01f);
                safi_inspector_property_float(ctx, "outer angle", &sl->outer_angle, 0.01f);
            }
        }

        /* ---- SafiRectLight ------------------------------------------- */
        if (ecs_has(world, sel, SafiRectLight)) {
            SafiRectLight *rl = ecs_get_mut(world, sel,
                                             SafiRectLight);
            if (mu_header_ex(ctx, "Rect Light", MU_OPT_EXPANDED)) {
                safi_inspector_property_vec3(ctx, "Color", rl->color, 0.05f);
                safi_inspector_property_float(ctx, "intensity", &rl->intensity, 0.1f);
                safi_inspector_property_float(ctx, "width",     &rl->width, 0.1f);
                safi_inspector_property_float(ctx, "height",    &rl->height, 0.1f);
            }
        }

        /* ---- SafiSkyLight -------------------------------------------- */
        if (ecs_has(world, sel, SafiSkyLight)) {
            SafiSkyLight *sk = ecs_get_mut(world, sel,
                                            SafiSkyLight);
            if (mu_header_ex(ctx, "Sky Light", MU_OPT_EXPANDED)) {
                safi_inspector_property_vec3(ctx, "Color", sk->color, 0.05f);
                safi_inspector_property_float(ctx, "intensity", &sk->intensity, 0.1f);
            }
        }
        mu_end_window(ctx);
    }

    /* Floating dropdown (if any) — drawn as its own root container after
     * the Inspector window so it isn't clipped by the Inspector body. */
    s_draw_open_dropdown(ctx);
}
