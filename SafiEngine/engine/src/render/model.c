#include "safi/render/model.h"
#include "safi/render/mesh.h"
#include "safi/render/shader.h"
#include "safi/core/log.h"

#include <cgltf.h>
#include <stb_image.h>

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

/* ---- GPU upload helpers ------------------------------------------------- */

static bool s_upload_buffer(SafiRenderer  *r,
                            SDL_GPUBuffer *dst,
                            const void    *data,
                            uint32_t       size) {
    SDL_GPUTransferBufferCreateInfo tbi = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = size,
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(r->device, &tbi);
    if (!tb) return false;

    void *mapped = SDL_MapGPUTransferBuffer(r->device, tb, false);
    memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(r->device, tb);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(r->device);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = { .transfer_buffer = tb, .offset = 0 };
    SDL_GPUBufferRegion region = { .buffer = dst, .offset = 0, .size = size };
    SDL_UploadToGPUBuffer(copy, &src, &region, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(r->device, tb);
    return true;
}

static SDL_GPUTexture *s_upload_rgba8(SafiRenderer  *r,
                                      const uint8_t *pixels,
                                      uint32_t       w,
                                      uint32_t       h) {
    SDL_GPUTextureCreateInfo ti = {
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .width                = w,
        .height               = h,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
        .sample_count         = SDL_GPU_SAMPLECOUNT_1,
        .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER,
    };
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(r->device, &ti);
    if (!tex) return NULL;

    uint32_t byte_size = w * h * 4u;
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
        .pixels_per_row  = w,
        .rows_per_layer  = h,
    };
    SDL_GPUTextureRegion dst = { .texture = tex, .w = w, .h = h, .d = 1 };
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(r->device, tb);
    return tex;
}

/* ---- material index map ------------------------------------------------- */

/* Map a cgltf_material pointer to a dense index. Returns the index.
 * If the material is new, appends it and increments *count. */
static uint32_t s_material_index(const cgltf_material  *mat,
                                 const cgltf_material **map,
                                 uint32_t              *count) {
    for (uint32_t i = 0; i < *count; i++) {
        if (map[i] == mat) return i;
    }
    uint32_t idx = *count;
    map[idx] = mat;
    (*count)++;
    return idx;
}

/* ---- pipeline creation (mirrors material.c logic) ----------------------- */

static bool s_create_pipeline(SafiRenderer *r, const char *shader_dir,
                              SafiModel *out) {
    SDL_GPUShader *vs = safi_shader_load(r, shader_dir, "unlit", "vs_main",
                                         SAFI_SHADER_STAGE_VERTEX, 0, 1, 0, 0);
    SDL_GPUShader *fs = safi_shader_load(r, shader_dir, "unlit", "fs_main",
                                         SAFI_SHADER_STAGE_FRAGMENT, 1, 0, 0, 0);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(r->device, vs);
        if (fs) SDL_ReleaseGPUShader(r->device, fs);
        return false;
    }

    SDL_GPUVertexBufferDescription vbd = {
        .slot              = 0,
        .pitch             = sizeof(SafiVertex),
        .input_rate        = SDL_GPU_VERTEXINPUTRATE_VERTEX,
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
        .blend_state = { .enable_blend = false },
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
        SAFI_LOG_ERROR("model pipeline create failed: %s", SDL_GetError());
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

    static const uint8_t white[4] = { 255, 255, 255, 255 };
    out->white_fallback = s_upload_rgba8(r, white, 1, 1);
    return out->white_fallback != NULL;
}

/* ---- texture loading from cgltf material -------------------------------- */

static SDL_GPUTexture *s_load_material_texture(SafiRenderer         *r,
                                               const cgltf_material *mat) {
    if (!mat || !mat->has_pbr_metallic_roughness) return NULL;

    const cgltf_pbr_metallic_roughness *pbr = &mat->pbr_metallic_roughness;
    const cgltf_texture *tex = pbr->base_color_texture.texture;

    /* If there's a texture image, load it. */
    if (tex && tex->image) {
        int w, h, n;
        uint8_t *pixels = NULL;

        if (tex->image->buffer_view) {
            const cgltf_buffer_view *bv = tex->image->buffer_view;
            const uint8_t *img = (const uint8_t *)bv->buffer->data + bv->offset;
            pixels = stbi_load_from_memory(img, (int)bv->size, &w, &h, &n, 4);
        } else if (tex->image->uri) {
            pixels = stbi_load(tex->image->uri, &w, &h, &n, 4);
        }

        if (pixels) {
            SDL_GPUTexture *gpu_tex = s_upload_rgba8(r, pixels,
                                                      (uint32_t)w, (uint32_t)h);
            stbi_image_free(pixels);
            if (gpu_tex)
                SAFI_LOG_INFO("model: loaded base-color texture %dx%d", w, h);
            return gpu_tex;
        }
        SAFI_LOG_WARN("model: base-color decode failed: %s",
                      stbi_failure_reason());
    }

    /* No texture — use baseColorFactor as a 1x1 solid-color texture. */
    const float *f = pbr->base_color_factor;
    uint8_t rgba[4] = {
        (uint8_t)(f[0] * 255.0f + 0.5f),
        (uint8_t)(f[1] * 255.0f + 0.5f),
        (uint8_t)(f[2] * 255.0f + 0.5f),
        (uint8_t)(f[3] * 255.0f + 0.5f),
    };
    SAFI_LOG_INFO("model: material '%s' using baseColorFactor [%u,%u,%u,%u]",
                  mat->name ? mat->name : "?",
                  rgba[0], rgba[1], rgba[2], rgba[3]);
    return s_upload_rgba8(r, rgba, 1, 1);
}

/* ---- lit pipeline creation ----------------------------------------------- */

static bool s_create_pipeline_lit(SafiRenderer *r, const char *shader_dir,
                                  SafiModel *out) {
    SDL_GPUShader *vs = safi_shader_load(r, shader_dir, "lit", "vs_main",
                                         SAFI_SHADER_STAGE_VERTEX, 0, 1, 0, 0);
    SDL_GPUShader *fs = safi_shader_load(r, shader_dir, "lit", "fs_main",
                                         SAFI_SHADER_STAGE_FRAGMENT, 1, 2, 0, 0);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(r->device, vs);
        if (fs) SDL_ReleaseGPUShader(r->device, fs);
        return false;
    }

    SDL_GPUVertexBufferDescription vbd = {
        .slot              = 0,
        .pitch             = sizeof(SafiVertex),
        .input_rate        = SDL_GPU_VERTEXINPUTRATE_VERTEX,
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
        .blend_state = { .enable_blend = false },
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
            .cull_mode  = SDL_GPU_CULLMODE_BACK,
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
        SAFI_LOG_ERROR("lit model pipeline create failed: %s", SDL_GetError());
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

    static const uint8_t white[4] = { 255, 255, 255, 255 };
    out->white_fallback = s_upload_rgba8(r, white, 1, 1);
    return out->white_fallback != NULL;
}

/* ---- public API --------------------------------------------------------- */

bool safi_model_load(SafiRenderer *r,
                     const char   *path,
                     const char   *shader_dir,
                     SafiModel    *out) {
    memset(out, 0, sizeof(*out));

    cgltf_options opts = {0};
    cgltf_data *data = NULL;
    if (cgltf_parse_file(&opts, path, &data) != cgltf_result_success) {
        SAFI_LOG_ERROR("cgltf_parse_file('%s') failed", path);
        return false;
    }
    if (cgltf_load_buffers(&opts, data, path) != cgltf_result_success) {
        SAFI_LOG_ERROR("cgltf_load_buffers failed");
        cgltf_free(data);
        return false;
    }

    /* --- First pass: count totals ---------------------------------------- */
    uint32_t total_verts = 0;
    uint32_t total_indices = 0;
    uint32_t total_prims = 0;

    for (cgltf_size mi = 0; mi < data->meshes_count; mi++) {
        const cgltf_mesh *mesh = &data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh->primitives_count; pi++) {
            const cgltf_primitive *prim = &mesh->primitives[pi];
            const cgltf_accessor *pos = NULL;
            for (cgltf_size ai = 0; ai < prim->attributes_count; ai++) {
                if (prim->attributes[ai].type == cgltf_attribute_type_position) {
                    pos = prim->attributes[ai].data;
                    break;
                }
            }
            if (!pos) continue;
            total_verts += (uint32_t)pos->count;
            total_indices += prim->indices
                ? (uint32_t)prim->indices->count
                : (uint32_t)pos->count;
            total_prims++;
        }
    }

    if (total_prims == 0) {
        SAFI_LOG_ERROR("glTF has no usable primitives");
        cgltf_free(data);
        return false;
    }

    /* --- Allocate CPU arrays --------------------------------------------- */
    SafiVertex *verts  = (SafiVertex *)calloc(total_verts, sizeof(SafiVertex));
    uint32_t   *indices = (uint32_t *)calloc(total_indices, sizeof(uint32_t));
    SafiModelPrimitive *prims = (SafiModelPrimitive *)calloc(total_prims,
                                    sizeof(SafiModelPrimitive));

    /* Material dedup map: up to data->materials_count + 1 (for NULL material) */
    uint32_t max_mats = (uint32_t)data->materials_count + 1;
    const cgltf_material **mat_map = (const cgltf_material **)calloc(
        max_mats, sizeof(cgltf_material *));
    uint32_t mat_count = 0;

    /* --- Second pass: fill geometry + record primitives ------------------- */
    uint32_t vert_cursor = 0;
    uint32_t idx_cursor  = 0;
    uint32_t prim_cursor = 0;

    /* AABB init */
    float aabb_min[3] = {  1e30f,  1e30f,  1e30f };
    float aabb_max[3] = { -1e30f, -1e30f, -1e30f };

    for (cgltf_size mi = 0; mi < data->meshes_count; mi++) {
        const cgltf_mesh *mesh = &data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh->primitives_count; pi++) {
            const cgltf_primitive *prim = &mesh->primitives[pi];
            const cgltf_accessor *pos_acc = NULL;
            const cgltf_accessor *nrm_acc = NULL;
            const cgltf_accessor *uv_acc  = NULL;
            for (cgltf_size ai = 0; ai < prim->attributes_count; ai++) {
                const cgltf_attribute *a = &prim->attributes[ai];
                if (a->type == cgltf_attribute_type_position)  pos_acc = a->data;
                else if (a->type == cgltf_attribute_type_normal)  nrm_acc = a->data;
                else if (a->type == cgltf_attribute_type_texcoord && a->index == 0)
                    uv_acc = a->data;
            }
            if (!pos_acc) continue;

            uint32_t vcount = (uint32_t)pos_acc->count;
            uint32_t vbase  = vert_cursor;

            /* Read vertices */
            for (uint32_t i = 0; i < vcount; i++) {
                SafiVertex *v = &verts[vert_cursor + i];
                float tmp[4] = {0};
                cgltf_accessor_read_float(pos_acc, i, tmp, 3);
                v->position[0] = tmp[0];
                v->position[1] = tmp[1];
                v->position[2] = tmp[2];
                for (int k = 0; k < 3; k++) {
                    if (tmp[k] < aabb_min[k]) aabb_min[k] = tmp[k];
                    if (tmp[k] > aabb_max[k]) aabb_max[k] = tmp[k];
                }
                if (nrm_acc) {
                    cgltf_accessor_read_float(nrm_acc, i, tmp, 3);
                    v->normal[0] = tmp[0];
                    v->normal[1] = tmp[1];
                    v->normal[2] = tmp[2];
                } else {
                    v->normal[1] = 1.0f;
                }
                if (uv_acc) {
                    cgltf_accessor_read_float(uv_acc, i, tmp, 2);
                    v->uv[0] = tmp[0];
                    v->uv[1] = tmp[1];
                }
            }
            vert_cursor += vcount;

            /* Read indices */
            uint32_t icount;
            uint32_t idx_start = idx_cursor;
            if (prim->indices) {
                icount = (uint32_t)prim->indices->count;
                for (uint32_t i = 0; i < icount; i++) {
                    indices[idx_cursor + i] =
                        vbase + (uint32_t)cgltf_accessor_read_index(prim->indices, i);
                }
            } else {
                icount = vcount;
                for (uint32_t i = 0; i < icount; i++) {
                    indices[idx_cursor + i] = vbase + i;
                }
            }
            idx_cursor += icount;

            /* Material index */
            uint32_t mat_idx = s_material_index(prim->material, mat_map, &mat_count);

            prims[prim_cursor++] = (SafiModelPrimitive){
                .index_offset   = idx_start,
                .index_count    = icount,
                .material_index = mat_idx,
            };
        }
    }

    /* --- Upload geometry ------------------------------------------------- */
    uint32_t vb_size = vert_cursor * (uint32_t)sizeof(SafiVertex);
    uint32_t ib_size = idx_cursor  * (uint32_t)sizeof(uint32_t);

    SDL_GPUBufferCreateInfo vbi = { .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = vb_size };
    out->vbo = SDL_CreateGPUBuffer(r->device, &vbi);
    SDL_GPUBufferCreateInfo ibi = { .usage = SDL_GPU_BUFFERUSAGE_INDEX,  .size = ib_size };
    out->ibo = SDL_CreateGPUBuffer(r->device, &ibi);

    if (!out->vbo || !out->ibo ||
        !s_upload_buffer(r, out->vbo, verts, vb_size) ||
        !s_upload_buffer(r, out->ibo, indices, ib_size)) {
        SAFI_LOG_ERROR("model: GPU buffer upload failed");
        free(verts); free(indices); free(prims); free(mat_map);
        cgltf_free(data);
        safi_model_destroy(r, out);
        return false;
    }
    out->vertex_count = vert_cursor;
    out->index_count  = idx_cursor;
    memcpy(out->aabb_min, aabb_min, sizeof(aabb_min));
    memcpy(out->aabb_max, aabb_max, sizeof(aabb_max));

    free(verts);
    free(indices);

    /* --- Primitives ------------------------------------------------------ */
    out->primitives      = prims;
    out->primitive_count = prim_cursor;

    /* --- Pipeline + sampler + fallback ----------------------------------- */
    if (!s_create_pipeline(r, shader_dir, out)) {
        free(mat_map);
        cgltf_free(data);
        safi_model_destroy(r, out);
        return false;
    }

    /* --- Load per-material textures -------------------------------------- */
    out->material_count = mat_count;
    out->base_colors = (SDL_GPUTexture **)calloc(mat_count, sizeof(SDL_GPUTexture *));
    for (uint32_t i = 0; i < mat_count; i++) {
        out->base_colors[i] = s_load_material_texture(r, mat_map[i]);
    }

    SAFI_LOG_INFO("model '%s': %u verts, %u indices, %u primitives, %u materials",
                  path, out->vertex_count, out->index_count,
                  out->primitive_count, out->material_count);

    free(mat_map);
    cgltf_free(data);
    return true;
}

void safi_model_draw(SafiRenderer    *r,
                     const SafiModel *m,
                     const float     *mvp) {
    SDL_BindGPUGraphicsPipeline(r->pass, m->pipeline);

    SDL_GPUBufferBinding vbind = { .buffer = m->vbo, .offset = 0 };
    SDL_BindGPUVertexBuffers(r->pass, 0, &vbind, 1);
    SDL_GPUBufferBinding ibind = { .buffer = m->ibo, .offset = 0 };
    SDL_BindGPUIndexBuffer(r->pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    SDL_PushGPUVertexUniformData(r->cmd, 0, mvp, 64);

    for (uint32_t i = 0; i < m->primitive_count; i++) {
        const SafiModelPrimitive *p = &m->primitives[i];
        SDL_GPUTexture *tex = (p->material_index < m->material_count)
            ? m->base_colors[p->material_index]
            : NULL;
        if (!tex) tex = m->white_fallback;

        SDL_GPUTextureSamplerBinding sb = { .texture = tex, .sampler = m->sampler };
        SDL_BindGPUFragmentSamplers(r->pass, 0, &sb, 1);

        SDL_DrawGPUIndexedPrimitives(r->pass, p->index_count, 1,
                                      p->index_offset, 0, 0);
    }
}

bool safi_model_load_lit(SafiRenderer *r,
                         const char   *path,
                         const char   *shader_dir,
                         SafiModel    *out) {
    /* Reuse the standard loader for geometry + textures but swap the pipeline. */
    if (!safi_model_load(r, path, shader_dir, out)) return false;

    /* Release the unlit pipeline and create a lit one instead. */
    if (out->pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(r->device, out->pipeline);
        out->pipeline = NULL;
    }
    if (out->sampler) {
        SDL_ReleaseGPUSampler(r->device, out->sampler);
        out->sampler = NULL;
    }
    if (out->white_fallback) {
        SDL_ReleaseGPUTexture(r->device, out->white_fallback);
        out->white_fallback = NULL;
    }

    if (!s_create_pipeline_lit(r, shader_dir, out)) {
        safi_model_destroy(r, out);
        return false;
    }
    return true;
}

void safi_model_draw_lit(SafiRenderer            *r,
                         const SafiModel          *m,
                         const SafiLitVSUniforms  *vs_uniforms,
                         const SafiCameraBuffer   *camera,
                         const SafiLightBuffer    *lights) {
    SDL_BindGPUGraphicsPipeline(r->pass, m->pipeline);

    SDL_GPUBufferBinding vbind = { .buffer = m->vbo, .offset = 0 };
    SDL_BindGPUVertexBuffers(r->pass, 0, &vbind, 1);
    SDL_GPUBufferBinding ibind = { .buffer = m->ibo, .offset = 0 };
    SDL_BindGPUIndexBuffer(r->pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_32BIT);

    /* Vertex uniform slot 0: model + mvp + normal_mat */
    SDL_PushGPUVertexUniformData(r->cmd, 0, vs_uniforms, sizeof(*vs_uniforms));

    /* Fragment uniform slot 0: camera buffer */
    SDL_PushGPUFragmentUniformData(r->cmd, 0, camera, sizeof(*camera));

    /* Fragment uniform slot 1: light buffer */
    SDL_PushGPUFragmentUniformData(r->cmd, 1, lights, sizeof(*lights));

    for (uint32_t i = 0; i < m->primitive_count; i++) {
        const SafiModelPrimitive *p = &m->primitives[i];
        SDL_GPUTexture *tex = (p->material_index < m->material_count)
            ? m->base_colors[p->material_index]
            : NULL;
        if (!tex) tex = m->white_fallback;

        SDL_GPUTextureSamplerBinding sb = { .texture = tex, .sampler = m->sampler };
        SDL_BindGPUFragmentSamplers(r->pass, 0, &sb, 1);

        SDL_DrawGPUIndexedPrimitives(r->pass, p->index_count, 1,
                                      p->index_offset, 0, 0);
    }
}

void safi_model_destroy(SafiRenderer *r, SafiModel *m) {
    if (!m) return;
    if (m->vbo)            SDL_ReleaseGPUBuffer(r->device, m->vbo);
    if (m->ibo)            SDL_ReleaseGPUBuffer(r->device, m->ibo);
    if (m->pipeline)       SDL_ReleaseGPUGraphicsPipeline(r->device, m->pipeline);
    if (m->sampler)        SDL_ReleaseGPUSampler(r->device, m->sampler);
    if (m->white_fallback) SDL_ReleaseGPUTexture(r->device, m->white_fallback);
    if (m->base_colors) {
        for (uint32_t i = 0; i < m->material_count; i++) {
            if (m->base_colors[i])
                SDL_ReleaseGPUTexture(r->device, m->base_colors[i]);
        }
        free(m->base_colors);
    }
    free(m->primitives);
    memset(m, 0, sizeof(*m));
}
