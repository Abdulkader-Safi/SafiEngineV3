/**
 * safi/render/primitive_mesh.h — procedural primitive geometry generators.
 *
 * Each builder fills freshly-malloc'd vertex and index arrays in the engine's
 * SafiVertex layout (position / normal / uv). The caller passes the arrays to
 * safi_mesh_create(), then free()'s both buffers.
 *
 * These are the raw geometry generators. For an ECS-integrated primitive that
 * also tracks color + texture and auto-creates its GPU resources, use the
 * SafiPrimitive component (see safi/ecs/components.h).
 */
#ifndef SAFI_RENDER_PRIMITIVE_MESH_H
#define SAFI_RENDER_PRIMITIVE_MESH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "safi/render/mesh.h"

typedef enum SafiPrimitiveShape {
    SAFI_PRIMITIVE_PLANE   = 0,
    SAFI_PRIMITIVE_BOX     = 1,
    SAFI_PRIMITIVE_SPHERE  = 2,
    SAFI_PRIMITIVE_CAPSULE = 3,
} SafiPrimitiveShape;

/* XZ-aligned plane centered at the origin, normals pointing +Y. 2 triangles. */
bool safi_primitive_build_plane(float          size,
                                SafiVertex   **out_verts,
                                size_t        *out_vcount,
                                uint32_t     **out_indices,
                                size_t        *out_icount);

/* Axis-aligned box centered at the origin. 24 verts (per-face normals so
 * lighting is flat), 36 indices. */
bool safi_primitive_build_box(const float    half_extents[3],
                              SafiVertex   **out_verts,
                              size_t        *out_vcount,
                              uint32_t     **out_indices,
                              size_t        *out_icount);

/* UV sphere centered at the origin. segments >= 3 (longitudinal slices),
 * rings >= 2 (latitudinal stacks). Equirectangular UVs. */
bool safi_primitive_build_sphere(float          radius,
                                 int            segments,
                                 int            rings,
                                 SafiVertex   **out_verts,
                                 size_t        *out_vcount,
                                 uint32_t     **out_indices,
                                 size_t        *out_icount);

/* Y-axis capsule centered at the origin: a cylinder of total length `height`
 * capped by two hemispheres of radius `radius`. Overall span along Y is
 * height + 2 * radius. segments >= 3, rings >= 2 (per hemisphere). */
bool safi_primitive_build_capsule(float          radius,
                                  float          height,
                                  int            segments,
                                  int            rings,
                                  SafiVertex   **out_verts,
                                  size_t        *out_vcount,
                                  uint32_t     **out_indices,
                                  size_t        *out_icount);

#endif /* SAFI_RENDER_PRIMITIVE_MESH_H */
