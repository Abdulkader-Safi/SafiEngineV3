#include "safi/render/mesh.h"
#include "safi/core/log.h"

#include <SDL3/SDL.h>
#include <string.h>

static bool s_upload(SafiRenderer  *r,
                     SDL_GPUBuffer *dst,
                     const void    *data,
                     uint32_t       size) {
    SDL_GPUTransferBufferCreateInfo tbinfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size  = size,
    };
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(r->device, &tbinfo);
    if (!tb) return false;

    void *mapped = SDL_MapGPUTransferBuffer(r->device, tb, /*cycle=*/false);
    memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(r->device, tb);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(r->device);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);

    SDL_GPUTransferBufferLocation src = { .transfer_buffer = tb, .offset = 0 };
    SDL_GPUBufferRegion region = { .buffer = dst, .offset = 0, .size = size };
    SDL_UploadToGPUBuffer(copy, &src, &region, /*cycle=*/false);

    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(r->device, tb);
    return true;
}

bool safi_mesh_create(SafiRenderer     *r,
                      SafiMesh         *out,
                      const SafiVertex *vertices,
                      uint32_t          vertex_count,
                      const uint32_t   *indices,
                      uint32_t          index_count) {
    memset(out, 0, sizeof(*out));

    uint32_t vb_size = vertex_count * (uint32_t)sizeof(SafiVertex);
    uint32_t ib_size = index_count  * (uint32_t)sizeof(uint32_t);

    SDL_GPUBufferCreateInfo vbi = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size  = vb_size,
    };
    out->vbo = SDL_CreateGPUBuffer(r->device, &vbi);
    if (!out->vbo) { SAFI_LOG_ERROR("VBO create failed: %s", SDL_GetError()); return false; }

    SDL_GPUBufferCreateInfo ibi = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size  = ib_size,
    };
    out->ibo = SDL_CreateGPUBuffer(r->device, &ibi);
    if (!out->ibo) { SAFI_LOG_ERROR("IBO create failed: %s", SDL_GetError()); return false; }

    if (!s_upload(r, out->vbo, vertices, vb_size)) return false;
    if (!s_upload(r, out->ibo, indices,  ib_size)) return false;

    out->vertex_count = vertex_count;
    out->index_count  = index_count;
    return true;
}

void safi_mesh_destroy(SafiRenderer *r, SafiMesh *mesh) {
    if (!mesh) return;
    if (mesh->vbo) SDL_ReleaseGPUBuffer(r->device, mesh->vbo);
    if (mesh->ibo) SDL_ReleaseGPUBuffer(r->device, mesh->ibo);
    memset(mesh, 0, sizeof(*mesh));
}
