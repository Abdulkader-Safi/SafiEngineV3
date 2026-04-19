#include "safi/render/camera.h"

#include <cglm/cglm.h>
#include <stdbool.h>

void safi_camera_build_view_proj(const SafiCamera *cam,
                                 int screen_w, int screen_h,
                                 mat4 out_view, mat4 out_proj) {
    if (!cam || screen_w <= 0 || screen_h <= 0) {
        glm_mat4_identity(out_view);
        glm_mat4_identity(out_proj);
        return;
    }

    vec3 eye, center, up;
    if (glm_vec3_norm2((float *)cam->eye) > 0.0f) {
        glm_vec3_copy((float *)cam->eye, eye);
        glm_vec3_add((float *)cam->eye, (float *)cam->forward, center);
        glm_vec3_copy((float *)cam->up, up);
    } else {
        /* Legacy convention — must stay in lockstep with render_system.c. */
        eye[0] = cam->target[0];
        eye[1] = cam->target[1];
        eye[2] = cam->target[2] + 3.0f;
        glm_vec3_zero(center);
        up[0] = 0.0f; up[1] = 1.0f; up[2] = 0.0f;
    }
    glm_lookat(eye, center, up, out_view);

    float aspect = (float)screen_w / (float)screen_h;
    glm_perspective(cam->fov_y_radians, aspect, cam->z_near, cam->z_far, out_proj);
}

void safi_camera_screen_ray(const SafiCamera *cam,
                            int screen_w, int screen_h,
                            float cursor_x, float cursor_y,
                            vec3 out_origin, vec3 out_dir) {
    mat4 view, proj, vp, inv_vp;
    safi_camera_build_view_proj(cam, screen_w, screen_h, view, proj);
    glm_mat4_mul(proj, view, vp);
    glm_mat4_inv(vp, inv_vp);

    /* Origin = camera eye. Same branch as build_view_proj above. */
    if (glm_vec3_norm2((float *)cam->eye) > 0.0f) {
        glm_vec3_copy((float *)cam->eye, out_origin);
    } else {
        out_origin[0] = cam->target[0];
        out_origin[1] = cam->target[1];
        out_origin[2] = cam->target[2] + 3.0f;
    }

    /* Pixel → NDC (SDL origin is top-left, flip Y). */
    float nx = (2.0f * cursor_x / (float)screen_w) - 1.0f;
    float ny = 1.0f - (2.0f * cursor_y / (float)screen_h);

    /* Unproject a far-plane point, then build dir = normalise(far - origin). */
    vec4 ndc = {nx, ny, 1.0f, 1.0f};
    vec4 world;
    glm_mat4_mulv(inv_vp, ndc, world);
    if (world[3] != 0.0f) {
        world[0] /= world[3];
        world[1] /= world[3];
        world[2] /= world[3];
    }
    vec3 far_pt = {world[0], world[1], world[2]};
    glm_vec3_sub(far_pt, out_origin, out_dir);
    glm_vec3_normalize(out_dir);
}

bool safi_camera_world_to_screen(const SafiCamera *cam,
                                 int screen_w, int screen_h,
                                 const float world[3],
                                 float *out_x, float *out_y) {
    if (!cam || screen_w <= 0 || screen_h <= 0 || !world) return false;

    mat4 view, proj, vp;
    safi_camera_build_view_proj(cam, screen_w, screen_h, view, proj);
    glm_mat4_mul(proj, view, vp);

    vec4 in_pt = { world[0], world[1], world[2], 1.0f };
    vec4 clip;
    glm_mat4_mulv(vp, in_pt, clip);

    /* w <= 0 means the point is behind (or on) the near plane. Reject so
     * callers don't see a bogus flipped projection. */
    if (clip[3] <= 1e-6f) return false;

    float inv_w = 1.0f / clip[3];
    float ndc_x = clip[0] * inv_w;
    float ndc_y = clip[1] * inv_w;

    if (out_x) *out_x = (ndc_x * 0.5f + 0.5f) * (float)screen_w;
    /* SDL origin: top-left, so flip Y (NDC +Y is up, pixels +Y is down). */
    if (out_y) *out_y = (1.0f - (ndc_y * 0.5f + 0.5f)) * (float)screen_h;
    return true;
}
