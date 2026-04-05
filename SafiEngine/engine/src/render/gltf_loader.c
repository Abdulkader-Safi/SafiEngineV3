#include "safi/render/gltf_loader.h"
#include "safi/core/log.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool safi_gltf_load(SafiRenderer *r,
                    const char   *path,
                    SafiMesh     *out_mesh,
                    SafiMaterial *inout_material) {
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

    if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0) {
        SAFI_LOG_ERROR("glTF has no meshes/primitives");
        cgltf_free(data);
        return false;
    }

    const cgltf_primitive *prim = &data->meshes[0].primitives[0];

    const cgltf_accessor *pos_acc = NULL;
    const cgltf_accessor *nrm_acc = NULL;
    const cgltf_accessor *uv_acc  = NULL;
    for (cgltf_size i = 0; i < prim->attributes_count; ++i) {
        const cgltf_attribute *a = &prim->attributes[i];
        if (a->type == cgltf_attribute_type_position) pos_acc = a->data;
        else if (a->type == cgltf_attribute_type_normal) nrm_acc = a->data;
        else if (a->type == cgltf_attribute_type_texcoord && a->index == 0) uv_acc = a->data;
    }
    if (!pos_acc) { cgltf_free(data); return false; }

    size_t vcount = pos_acc->count;
    SafiVertex *verts = (SafiVertex *)calloc(vcount, sizeof(SafiVertex));

    /* Use cgltf_accessor_read_float for safety across strides/types. */
    for (size_t i = 0; i < vcount; ++i) {
        float tmp[4] = {0};
        cgltf_accessor_read_float(pos_acc, i, tmp, 3);
        verts[i].position[0] = tmp[0];
        verts[i].position[1] = tmp[1];
        verts[i].position[2] = tmp[2];
        if (nrm_acc) {
            cgltf_accessor_read_float(nrm_acc, i, tmp, 3);
            verts[i].normal[0] = tmp[0];
            verts[i].normal[1] = tmp[1];
            verts[i].normal[2] = tmp[2];
        } else {
            verts[i].normal[1] = 1.0f;
        }
        if (uv_acc) {
            cgltf_accessor_read_float(uv_acc, i, tmp, 2);
            verts[i].uv[0] = tmp[0];
            verts[i].uv[1] = tmp[1];
        }
    }

    size_t icount = prim->indices ? prim->indices->count : 0;
    uint32_t *indices = (uint32_t *)calloc(icount ? icount : 1, sizeof(uint32_t));
    if (prim->indices) {
        for (size_t i = 0; i < icount; ++i) {
            indices[i] = (uint32_t)cgltf_accessor_read_index(prim->indices, i);
        }
    } else {
        /* No indices: generate a trivial 0..N-1. */
        icount = vcount;
        indices = (uint32_t *)realloc(indices, icount * sizeof(uint32_t));
        for (size_t i = 0; i < icount; ++i) indices[i] = (uint32_t)i;
    }

    bool ok = safi_mesh_create(r, out_mesh, verts, (uint32_t)vcount,
                               indices, (uint32_t)icount);
    SAFI_LOG_INFO("glTF '%s': %zu verts, %zu indices, mesh_ok=%d",
                  path, vcount, icount, (int)ok);
    free(verts);
    free(indices);

    /* Base color texture, if any. */
    if (ok && inout_material && prim->material) {
        const cgltf_texture *tex = prim->material->pbr_metallic_roughness
                                       .base_color_texture.texture;
        if (tex && tex->image && tex->image->buffer_view) {
            const cgltf_buffer_view *bv = tex->image->buffer_view;
            const uint8_t *img = (const uint8_t *)bv->buffer->data + bv->offset;
            int w, h, n;
            uint8_t *pixels = stbi_load_from_memory(img, (int)bv->size,
                                                    &w, &h, &n, 4);
            if (pixels) {
                safi_material_set_base_color_rgba8(r, inout_material, pixels,
                                                   (uint32_t)w, (uint32_t)h);
                SAFI_LOG_INFO("glTF base-color texture: %dx%d (embedded)", w, h);
                stbi_image_free(pixels);
            } else {
                SAFI_LOG_WARN("glTF base-color decode failed: %s", stbi_failure_reason());
            }
        } else if (tex && tex->image && tex->image->uri) {
            int w, h, n;
            /* uri relative to gltf file — best-effort resolution. */
            char full[1024];
            snprintf(full, sizeof(full), "%s", tex->image->uri);
            uint8_t *pixels = stbi_load(full, &w, &h, &n, 4);
            if (pixels) {
                safi_material_set_base_color_rgba8(r, inout_material, pixels,
                                                   (uint32_t)w, (uint32_t)h);
                stbi_image_free(pixels);
            }
        }
    }

    cgltf_free(data);
    return ok;
}
