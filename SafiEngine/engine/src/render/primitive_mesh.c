#include "safi/render/primitive_mesh.h"

#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void s_set(SafiVertex *v,
                  float px, float py, float pz,
                  float nx, float ny, float nz,
                  float u,  float vv) {
    v->position[0] = px; v->position[1] = py; v->position[2] = pz;
    v->normal[0]   = nx; v->normal[1]   = ny; v->normal[2]   = nz;
    v->uv[0]       = u;  v->uv[1]       = vv;
}

/* ---- Plane -------------------------------------------------------------- */

bool safi_primitive_build_plane(float          size,
                                SafiVertex   **out_verts,
                                size_t        *out_vcount,
                                uint32_t     **out_indices,
                                size_t        *out_icount) {
    if (size <= 0.0f) return false;

    SafiVertex *v = (SafiVertex *)malloc(sizeof(SafiVertex) * 4);
    uint32_t   *i = (uint32_t   *)malloc(sizeof(uint32_t)   * 6);
    if (!v || !i) { free(v); free(i); return false; }

    float h = size * 0.5f;
    s_set(&v[0], -h, 0, -h, 0, 1, 0, 0, 0);
    s_set(&v[1],  h, 0, -h, 0, 1, 0, 1, 0);
    s_set(&v[2],  h, 0,  h, 0, 1, 0, 1, 1);
    s_set(&v[3], -h, 0,  h, 0, 1, 0, 0, 1);

    /* CCW when viewed from +Y, matching SAFI_FRONTFACE_CCW. */
    i[0] = 0; i[1] = 2; i[2] = 1;
    i[3] = 0; i[4] = 3; i[5] = 2;

    *out_verts   = v;   *out_vcount = 4;
    *out_indices = i;   *out_icount = 6;
    return true;
}

/* ---- Box ---------------------------------------------------------------- */

bool safi_primitive_build_box(const float    half_extents[3],
                              SafiVertex   **out_verts,
                              size_t        *out_vcount,
                              uint32_t     **out_indices,
                              size_t        *out_icount) {
    if (!half_extents) return false;
    float hx = half_extents[0], hy = half_extents[1], hz = half_extents[2];
    if (hx <= 0 || hy <= 0 || hz <= 0) return false;

    SafiVertex *v = (SafiVertex *)malloc(sizeof(SafiVertex) * 24);
    uint32_t   *i = (uint32_t   *)malloc(sizeof(uint32_t)   * 36);
    if (!v || !i) { free(v); free(i); return false; }

    /* 6 faces × 4 unique verts each (per-face normals give flat shading). */
    int k = 0;
    /* +X face */
    s_set(&v[k++],  hx, -hy, -hz,  1,0,0, 0,0);
    s_set(&v[k++],  hx, -hy,  hz,  1,0,0, 1,0);
    s_set(&v[k++],  hx,  hy,  hz,  1,0,0, 1,1);
    s_set(&v[k++],  hx,  hy, -hz,  1,0,0, 0,1);
    /* -X face */
    s_set(&v[k++], -hx, -hy,  hz, -1,0,0, 0,0);
    s_set(&v[k++], -hx, -hy, -hz, -1,0,0, 1,0);
    s_set(&v[k++], -hx,  hy, -hz, -1,0,0, 1,1);
    s_set(&v[k++], -hx,  hy,  hz, -1,0,0, 0,1);
    /* +Y face. Vertex order is "walk clockwise when viewed from above" so
     * the shared (0,2,1)/(0,3,2) index pattern produces an outward (+Y)
     * normal under the right-hand rule — a +Y face viewed from outside
     * (above) traces CW in the +X/+Z screen, which is CCW from the +Y
     * direction. The other four side faces already satisfy the same rule
     * with their natural vertex order, only the Y caps needed flipping. */
    s_set(&v[k++], -hx,  hy,  hz, 0,1,0, 0,0);
    s_set(&v[k++], -hx,  hy, -hz, 0,1,0, 0,1);
    s_set(&v[k++],  hx,  hy, -hz, 0,1,0, 1,1);
    s_set(&v[k++],  hx,  hy,  hz, 0,1,0, 1,0);
    /* -Y face — mirror of +Y. */
    s_set(&v[k++], -hx, -hy, -hz, 0,-1,0, 0,0);
    s_set(&v[k++], -hx, -hy,  hz, 0,-1,0, 0,1);
    s_set(&v[k++],  hx, -hy,  hz, 0,-1,0, 1,1);
    s_set(&v[k++],  hx, -hy, -hz, 0,-1,0, 1,0);
    /* +Z face */
    s_set(&v[k++],  hx, -hy,  hz, 0,0,1, 0,0);
    s_set(&v[k++], -hx, -hy,  hz, 0,0,1, 1,0);
    s_set(&v[k++], -hx,  hy,  hz, 0,0,1, 1,1);
    s_set(&v[k++],  hx,  hy,  hz, 0,0,1, 0,1);
    /* -Z face */
    s_set(&v[k++], -hx, -hy, -hz, 0,0,-1, 0,0);
    s_set(&v[k++],  hx, -hy, -hz, 0,0,-1, 1,0);
    s_set(&v[k++],  hx,  hy, -hz, 0,0,-1, 1,1);
    s_set(&v[k++], -hx,  hy, -hz, 0,0,-1, 0,1);

    /* CCW winding when looking at each face from outside. The face verts
     * above are listed as (BL, BR, TR, TL) in the exterior's screen space
     * for each face, so the outward-CCW triangulation is (0,2,1) and
     * (0,3,2). */
    uint32_t *ix = i;
    for (int f = 0; f < 6; f++) {
        uint32_t b = (uint32_t)(f * 4);
        *ix++ = b + 0; *ix++ = b + 2; *ix++ = b + 1;
        *ix++ = b + 0; *ix++ = b + 3; *ix++ = b + 2;
    }

    *out_verts   = v;   *out_vcount = 24;
    *out_indices = i;   *out_icount = 36;
    return true;
}

/* ---- Sphere (UV) -------------------------------------------------------- */

bool safi_primitive_build_sphere(float          radius,
                                 int            segments,
                                 int            rings,
                                 SafiVertex   **out_verts,
                                 size_t        *out_vcount,
                                 uint32_t     **out_indices,
                                 size_t        *out_icount) {
    if (radius <= 0.0f || segments < 3 || rings < 2) return false;

    size_t vcount = (size_t)(segments + 1) * (size_t)(rings + 1);
    size_t icount = (size_t)segments * (size_t)rings * 6u;

    SafiVertex *v = (SafiVertex *)malloc(sizeof(SafiVertex) * vcount);
    uint32_t   *i = (uint32_t   *)malloc(sizeof(uint32_t)   * icount);
    if (!v || !i) { free(v); free(i); return false; }

    for (int r = 0; r <= rings; r++) {
        float vu = (float)r / (float)rings;            /* 0..1 top→bottom */
        float phi = (float)M_PI * vu;                  /* 0..π */
        float sp = sinf(phi), cp = cosf(phi);
        for (int s = 0; s <= segments; s++) {
            float uu = (float)s / (float)segments;     /* 0..1 */
            float theta = 2.0f * (float)M_PI * uu;     /* 0..2π */
            float st = sinf(theta), ct = cosf(theta);

            float nx = sp * ct;
            float ny = cp;
            float nz = sp * st;

            size_t idx = (size_t)r * (size_t)(segments + 1) + (size_t)s;
            s_set(&v[idx],
                  radius * nx, radius * ny, radius * nz,
                  nx, ny, nz,
                  uu, vu);
        }
    }

    /* CCW from outside: for a quad (a=top-left, b=bottom-left, c=bottom-right,
     * d=top-right), the outward winding is (a, c, b) and (a, d, c). */
    uint32_t *ix = i;
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < segments; s++) {
            uint32_t a = (uint32_t)(r       * (segments + 1) + s);
            uint32_t b = (uint32_t)((r + 1) * (segments + 1) + s);
            uint32_t c = b + 1;
            uint32_t d = a + 1;
            *ix++ = a; *ix++ = c; *ix++ = b;
            *ix++ = a; *ix++ = d; *ix++ = c;
        }
    }

    *out_verts   = v;   *out_vcount = vcount;
    *out_indices = i;   *out_icount = icount;
    return true;
}

/* ---- Capsule ------------------------------------------------------------ */
/* Layout along Y: top hemisphere (rings_cap stacks), cylinder body
 * (1 stack), bottom hemisphere (rings_cap stacks). Hemispheres share
 * their equator ring with the cylinder ends. */

bool safi_primitive_build_capsule(float          radius,
                                  float          height,
                                  int            segments,
                                  int            rings,
                                  SafiVertex   **out_verts,
                                  size_t        *out_vcount,
                                  uint32_t     **out_indices,
                                  size_t        *out_icount) {
    if (radius <= 0.0f || height < 0.0f || segments < 3 || rings < 2)
        return false;

    int rings_cap = rings;            /* per hemisphere */
    int stacks    = rings_cap * 2 + 1; /* top cap + cylinder + bottom cap */

    size_t vcount = (size_t)(segments + 1) * (size_t)(stacks + 1);
    size_t icount = (size_t)segments * (size_t)stacks * 6u;

    SafiVertex *v = (SafiVertex *)malloc(sizeof(SafiVertex) * vcount);
    uint32_t   *i = (uint32_t   *)malloc(sizeof(uint32_t)   * icount);
    if (!v || !i) { free(v); free(i); return false; }

    float half_h = height * 0.5f;
    float total  = height + 2.0f * radius;

    for (int y = 0; y <= stacks; y++) {
        /* Determine which band this stack row belongs to. */
        float cy, ny, sy;      /* ring center Y, normal Y component, sin(phi) */
        if (y <= rings_cap) {
            /* Top hemisphere: phi 0..π/2, from pole down to +equator. */
            float phi = (float)M_PI * 0.5f * ((float)y / (float)rings_cap);
            sy = sinf(phi);
            ny = cosf(phi);
            cy = half_h + radius * ny;
        } else {
            /* Bottom hemisphere: phi π/2..π, from -equator down to pole. */
            int yb = y - (rings_cap + 1);   /* 0..rings_cap */
            float phi = (float)M_PI * 0.5f +
                        (float)M_PI * 0.5f * ((float)yb / (float)rings_cap);
            sy = sinf(phi);
            ny = cosf(phi);
            cy = -half_h + radius * ny;
        }

        /* Compute a V coordinate from world-space y so the texture stretches
         * continuously across the whole capsule. */
        float vu = 1.0f - ((cy + total * 0.5f) / total);

        for (int s = 0; s <= segments; s++) {
            float uu = (float)s / (float)segments;
            float theta = 2.0f * (float)M_PI * uu;
            float st = sinf(theta), ct = cosf(theta);

            float nx = sy * ct;
            float nz = sy * st;
            float px = radius * nx;
            float pz = radius * nz;

            size_t idx = (size_t)y * (size_t)(segments + 1) + (size_t)s;
            s_set(&v[idx], px, cy, pz, nx, ny, nz, uu, vu);
        }
    }

    /* Same outward-CCW quad split as the sphere. */
    uint32_t *ix = i;
    for (int y = 0; y < stacks; y++) {
        for (int s = 0; s < segments; s++) {
            uint32_t a = (uint32_t)(y       * (segments + 1) + s);
            uint32_t b = (uint32_t)((y + 1) * (segments + 1) + s);
            uint32_t c = b + 1;
            uint32_t d = a + 1;
            *ix++ = a; *ix++ = c; *ix++ = b;
            *ix++ = a; *ix++ = d; *ix++ = c;
        }
    }

    *out_verts   = v;   *out_vcount = vcount;
    *out_indices = i;   *out_icount = icount;
    return true;
}
