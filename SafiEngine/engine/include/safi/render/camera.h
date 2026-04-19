/**
 * safi/render/camera.h — camera math helpers.
 *
 * Shared utilities for building the view/projection matrices a SafiCamera
 * represents and for unprojecting screen-space cursor coordinates into
 * world-space rays (picking, gizmo drag, cursor snap, etc.). Kept in one
 * place so the render system and any editor / gameplay code all agree on
 * the same eye / forward / up convention.
 */
#ifndef SAFI_RENDER_CAMERA_H
#define SAFI_RENDER_CAMERA_H

#include "safi/ecs/components.h"

#include <cglm/cglm.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compute view and projection matrices for `cam` at the given viewport
 * dimensions. Prefers the explicit pose (`cam->eye`, `cam->forward`,
 * `cam->up`); when `cam->eye` is all-zero, falls back to the legacy
 * "eye = target + (0,0,3), look at origin" convention so older scenes
 * keep working. */
void safi_camera_build_view_proj(const SafiCamera *cam,
                                 int screen_w, int screen_h,
                                 mat4 out_view, mat4 out_proj);

/* Unproject a cursor position (pixel coords, SDL origin top-left) into a
 * world-space ray. `out_origin` is the camera eye; `out_dir` is the
 * unit-length direction from the eye through the far-plane point under the
 * cursor. Safe to call every frame. */
void safi_camera_screen_ray(const SafiCamera *cam,
                            int screen_w, int screen_h,
                            float cursor_x, float cursor_y,
                            vec3 out_origin, vec3 out_dir);

/* Project a world-space point to screen pixel coordinates (SDL origin:
 * top-left). Returns false if the point is behind the camera or at the
 * clip boundary; `out_x` / `out_y` are undefined in that case. */
bool safi_camera_world_to_screen(const SafiCamera *cam,
                                 int screen_w, int screen_h,
                                 const float world[3],
                                 float *out_x, float *out_y);

#ifdef __cplusplus
}
#endif

#endif /* SAFI_RENDER_CAMERA_H */
