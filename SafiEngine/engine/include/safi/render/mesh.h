/**
 * safi/render/mesh.h — GPU mesh (interleaved vertex + index buffers).
 */
#ifndef SAFI_RENDER_MESH_H
#define SAFI_RENDER_MESH_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL_gpu.h>

#include "safi/render/renderer.h"

typedef struct SafiVertex {
    float position[3];
    float normal[3];
    float uv[2];
} SafiVertex;

typedef struct SafiMesh {
    SDL_GPUBuffer *vbo;
    SDL_GPUBuffer *ibo;
    uint32_t       vertex_count;
    uint32_t       index_count;
} SafiMesh;

bool safi_mesh_create(SafiRenderer     *r,
                      SafiMesh         *out,
                      const SafiVertex *vertices,
                      uint32_t          vertex_count,
                      const uint32_t   *indices,
                      uint32_t          index_count);

void safi_mesh_destroy(SafiRenderer *r, SafiMesh *mesh);

#endif /* SAFI_RENDER_MESH_H */
