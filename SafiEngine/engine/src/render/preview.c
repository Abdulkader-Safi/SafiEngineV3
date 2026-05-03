#include "safi/render/preview.h"
#include "safi/render/light_buffer.h"

#include <cglm/cglm.h>
#include <string.h>

bool safi_preview_render_model(SafiRenderer   *r,
                               SafiModel      *model,
                               SDL_GPUTexture *target,
                               SDL_GPUTexture *depth,
                               uint32_t        width,
                               uint32_t        height) {
    if (!r || !r->cmd || !model || !target || width == 0 || height == 0) {
        return false;
    }

    /* Frame the AABB with an orthographic camera at a fixed three-quarter
     * angle. Treats degenerate AABBs (radius 0) as unit boxes so callers
     * can preview meshes whose builders haven't filled the AABB yet. */
    vec3 mn = { model->aabb_min[0], model->aabb_min[1], model->aabb_min[2] };
    vec3 mx = { model->aabb_max[0], model->aabb_max[1], model->aabb_max[2] };
    vec3 center;
    glm_vec3_add(mn, mx, center);
    glm_vec3_scale(center, 0.5f, center);

    vec3 ext;
    glm_vec3_sub(mx, mn, ext);
    float diag = glm_vec3_norm(ext);
    if (diag <= 1e-6f) diag = 1.0f;

    vec3 eye_dir = { 1.0f, 0.7f, 1.0f };
    glm_vec3_normalize(eye_dir);
    vec3 eye;
    glm_vec3_scale(eye_dir, diag * 1.5f, eye);
    glm_vec3_add(eye, center, eye);
    vec3 up = { 0.0f, 1.0f, 0.0f };

    mat4 view, proj;
    glm_lookat(eye, center, up, view);

    float half   = diag * 0.6f;
    float aspect = (float)width / (float)height;
    glm_ortho(-half * aspect, half * aspect, -half, half,
              0.01f, diag * 4.0f, proj);

    SafiCameraBuffer cam_buf;
    memcpy(cam_buf.view, view, sizeof(view));
    memcpy(cam_buf.proj, proj, sizeof(proj));
    cam_buf.eye_pos[0] = eye[0];
    cam_buf.eye_pos[1] = eye[1];
    cam_buf.eye_pos[2] = eye[2];
    cam_buf._pad = 0.0f;

    SafiLightBuffer lights;
    memset(&lights, 0, sizeof(lights));
    lights.lights[0].type         = SAFI_LIGHT_TYPE_DIRECTIONAL;
    lights.lights[0].direction[0] = -eye_dir[0];
    lights.lights[0].direction[1] = -eye_dir[1];
    lights.lights[0].direction[2] = -eye_dir[2];
    lights.lights[0].color[0]     = 1.0f;
    lights.lights[0].color[1]     = 1.0f;
    lights.lights[0].color[2]     = 1.0f;
    lights.lights[0].intensity    = 1.0f;
    lights.light_count            = 1;
    lights.ambient_color[0]       = 0.20f;
    lights.ambient_color[1]       = 0.20f;
    lights.ambient_color[2]       = 0.25f;
    lights.ambient_intensity      = 0.40f;

    SafiLitVSUniforms vs;
    mat4 model_mat;
    glm_mat4_identity(model_mat);
    memcpy(vs.model, model_mat, sizeof(model_mat));
    mat4 mvp;
    glm_mat4_mul(proj, view, mvp);
    memcpy(vs.mvp, mvp, sizeof(mvp));
    safi_compute_normal_matrix((const float *)model_mat, vs.normal_mat);

    safi_renderer_begin_offscreen_pass(r, target, depth, width, height);
    if (!r->pass) return false;
    safi_model_draw_lit(r, model, &vs, &cam_buf, &lights);
    safi_renderer_end_offscreen_pass(r);
    return true;
}
