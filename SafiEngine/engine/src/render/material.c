#include "safi/render/material.h"
#include "safi/render/shader.h"
#include "safi/render/mesh.h"
#include "safi/core/log.h"

#include <SDL3/SDL.h>
#include <string.h>

bool safi_material_create_unlit(SafiRenderer *r,
                                SafiMaterial *out,
                                const char   *shader_dir) {
    memset(out, 0, sizeof(*out));

    SDL_GPUShader *vs = safi_shader_load(r, shader_dir, "unlit", "vs_main",
                                         SAFI_SHADER_STAGE_VERTEX,
                                         0, 1, 0, 0);
    SDL_GPUShader *fs = safi_shader_load(r, shader_dir, "unlit", "fs_main",
                                         SAFI_SHADER_STAGE_FRAGMENT,
                                         1, 0, 0, 0);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(r->device, vs);
        if (fs) SDL_ReleaseGPUShader(r->device, fs);
        return false;
    }

    SDL_GPUVertexBufferDescription vbd = {
        .slot              = 0,
        .pitch             = sizeof(SafiVertex),
        .input_rate        = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };
    SDL_GPUVertexAttribute attrs[3] = {
        { .buffer_slot = 0, .location = 0,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
          .offset = offsetof(SafiVertex, position) },
        { .buffer_slot = 0, .location = 1,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
          .offset = offsetof(SafiVertex, normal) },
        { .buffer_slot = 0, .location = 2,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
          .offset = offsetof(SafiVertex, uv) },
    };

    SDL_GPUColorTargetDescription color_desc = {
        .format = r->swapchain_format,
        .blend_state = {
            .enable_blend = false,
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
            /* Keep culling off in the first-milestone unlit material so
             * arbitrary glTF models render regardless of their winding. */
            .cull_mode  = SDL_GPU_CULLMODE_NONE,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        },
        .multisample_state = { .sample_count = SDL_GPU_SAMPLECOUNT_1 },
        .depth_stencil_state = {
            .enable_depth_test  = true,
            .enable_depth_write = true,
            .compare_op         = SDL_GPU_COMPAREOP_LESS,
        },
        .target_info = {
            .color_target_descriptions = &color_desc,
            .num_color_targets         = 1,
            .depth_stencil_format      = r->depth_format,
            .has_depth_stencil_target  = true,
        },
    };

    out->pipeline = SDL_CreateGPUGraphicsPipeline(r->device, &pci);
    SDL_ReleaseGPUShader(r->device, vs);
    SDL_ReleaseGPUShader(r->device, fs);
    if (!out->pipeline) {
        SAFI_LOG_ERROR("pipeline create failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUSamplerCreateInfo si = {
        .min_filter     = SDL_GPU_FILTER_LINEAR,
        .mag_filter     = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
    };
    out->sampler = SDL_CreateGPUSampler(r->device, &si);
    if (!out->sampler) return false;

    /* Install a 1×1 opaque-white base color so the fragment shader always
     * has a valid sampler binding even when the glTF has no texture or its
     * upload fails. Without this, unbound samplers read as zero and the
     * whole mesh renders as transparent/black against the clear color. */
    static const uint8_t white_rgba[4] = { 255, 255, 255, 255 };
    safi_material_set_base_color_rgba8(r, out, white_rgba, 1, 1);
    return true;
}

void safi_material_destroy(SafiRenderer *r, SafiMaterial *m) {
    if (!m) return;
    if (m->pipeline)  SDL_ReleaseGPUGraphicsPipeline(r->device, m->pipeline);
    if (m->base_color) SDL_ReleaseGPUTexture(r->device, m->base_color);
    if (m->sampler)   SDL_ReleaseGPUSampler(r->device, m->sampler);
    memset(m, 0, sizeof(*m));
}

bool safi_material_set_base_color_rgba8(SafiRenderer  *r,
                                        SafiMaterial  *m,
                                        const uint8_t *pixels,
                                        uint32_t       width,
                                        uint32_t       height) {
    if (m->base_color) {
        SDL_ReleaseGPUTexture(r->device, m->base_color);
        m->base_color = NULL;
    }

    SDL_GPUTextureCreateInfo ti = {
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .width                = width,
        .height               = height,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
        .sample_count         = SDL_GPU_SAMPLECOUNT_1,
        .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER,
    };
    m->base_color = SDL_CreateGPUTexture(r->device, &ti);
    if (!m->base_color) return false;

    uint32_t byte_size = width * height * 4u;
    SDL_GPUTransferBufferCreateInfo tbi = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = byte_size,
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(r->device, &tbi);
    void *mapped = SDL_MapGPUTransferBuffer(r->device, tb, false);
    memcpy(mapped, pixels, byte_size);
    SDL_UnmapGPUTransferBuffer(r->device, tb);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(r->device);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTextureTransferInfo src = {
        .transfer_buffer = tb,
        .offset          = 0,
        .pixels_per_row  = width,
        .rows_per_layer  = height,
    };
    SDL_GPUTextureRegion dst = {
        .texture = m->base_color,
        .w = width, .h = height, .d = 1,
    };
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(r->device, tb);
    return true;
}
