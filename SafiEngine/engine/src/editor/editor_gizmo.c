/*
 * editor_gizmo.c — translate / rotate / scale manipulator handles.
 *
 * Runs on EcsOnUpdate in Edit mode: picks up the selected entity, reads
 * the active camera to size the handles by a screen-space metric, draws
 * the current tool's geometry into the gizmo draw list, and — when the
 * user left-clicks on an axis — starts a drag that mutates the entity's
 * `SafiTransform` each frame until the button is released.
 *
 * State is file-local: exactly one drag can be live at a time, which
 * matches the expected UX (click, drag, release, re-engage). The drag
 * carries the target entity's transform at drag-start and a pre-computed
 * inverse of the parent's world matrix so parented entities transform
 * correctly without re-inverting per frame.
 */

#include "safi/editor/editor_gizmo.h"
#include "safi/editor/editor_state.h"
#include "safi/ecs/change_bus.h"
#include "safi/ecs/components.h"
#include "safi/input/input.h"
#include "safi/render/camera.h"
#include "safi/render/gizmo.h"
#include "safi/ui/debug_ui.h"

#include <SDL3/SDL.h>
#include <cglm/cglm.h>
#include <math.h>

/* Axis colours (linear RGBA). */
static const float AXIS_COLOR[3][4] = {
    { 1.00f, 0.28f, 0.28f, 1.0f },   /* X — red     */
    { 0.32f, 0.95f, 0.32f, 1.0f },   /* Y — green   */
    { 0.36f, 0.50f, 1.00f, 1.0f },   /* Z — blue    */
};

/* Yellow highlight used while an axis is hovered or being dragged. */
static const float HIGHLIGHT_COLOR[4] = { 1.00f, 0.92f, 0.20f, 1.0f };

/* Unit axis vectors. */
static const float AXIS_DIR[3][3] = {
    { 1.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f },
};

/* Pixel distance inside which the cursor counts as "over" an axis. */
#define HIT_THRESHOLD_PX 12.0f

/* Minimum allowed scale on any axis — a careless drag shouldn't zero out
 * the transform and leave the entity unrenderable. */
#define SCALE_MIN 0.01f

/* Currently hovered axis (0..2), or -1 for no hover. Cleared every frame
 * and written by the hit-test pass. */
static int g_hovered_axis = -1;

/* In-flight drag. `axis == -1` means no drag is live. */
static struct {
    int            axis;
    SafiEditorTool tool;
    ecs_entity_t   target;
    vec3           origin_world;       /* entity pos at drag start            */
    vec3           current_world;      /* live world pos (translate updates it) */
    vec3           start_scale;        /* SafiTransform.scale at drag start    */
    versor         start_rotation;     /* SafiTransform.rotation at drag start */
    vec3           rotate_anchor;      /* normalised "start" vector on the ring plane */
    float          start_param;        /* t₀ along the axis ray (translate/scale) */
    mat4           parent_inv;         /* inverse(parent world) at drag start  */
} g_drag = { .axis = -1 };

/* -- math helpers -------------------------------------------------------- */

static float pt_to_segment_sq(float px, float py,
                              float ax, float ay,
                              float bx, float by) {
    float abx = bx - ax, aby = by - ay;
    float apx = px - ax, apy = py - ay;
    float ab_len2 = abx*abx + aby*aby;
    float t = (ab_len2 > 1e-6f) ? (apx*abx + apy*aby) / ab_len2 : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float cx = ax + abx * t;
    float cy = ay + aby * t;
    float dx = px - cx, dy = py - cy;
    return dx*dx + dy*dy;
}

/* Closest point on line 2 to line 1, returned as the parameter t along
 * line 2. Both directions must be unit length. Returns 0 when the lines
 * are near-parallel (denominator collapses). */
static float closest_param_on_line2(const vec3 o1, const vec3 d1,
                                    const vec3 o2, const vec3 d2) {
    vec3 w;
    glm_vec3_sub((float *)o1, (float *)o2, w);
    float a = glm_vec3_dot((float *)d1, (float *)d1);   /* = 1 */
    float b = glm_vec3_dot((float *)d1, (float *)d2);
    float c = glm_vec3_dot((float *)d2, (float *)d2);   /* = 1 */
    float d = glm_vec3_dot((float *)d1, w);
    float e = glm_vec3_dot((float *)d2, w);
    float denom = a*c - b*b;
    if (fabsf(denom) < 1e-6f) return 0.0f;
    return (a*e - b*d) / denom;
}

/* Ray-plane intersection. Plane = normal `n` through point `p`. Ray = origin
 * `o`, direction `d`. Writes the world-space hit into `out` and returns true,
 * or returns false when the ray is near-parallel to the plane. */
static bool ray_plane_intersect(const vec3 o, const vec3 d,
                                const vec3 p, const vec3 n,
                                vec3 out) {
    float denom = glm_vec3_dot((float *)d, (float *)n);
    if (fabsf(denom) < 1e-6f) return false;
    vec3 po;
    glm_vec3_sub((float *)p, (float *)o, po);
    float t = glm_vec3_dot(po, (float *)n) / denom;
    out[0] = o[0] + d[0] * t;
    out[1] = o[1] + d[1] * t;
    out[2] = o[2] + d[2] * t;
    return true;
}

/* Project a ring anchor vector: intersect the cursor ray with the plane
 * through `center` whose normal is the ring axis, subtract the centre to
 * get a vector in the plane, and normalise. Returns false when the ray is
 * parallel to the plane (rotate step should skip this frame). */
static bool compute_ring_anchor(const SafiCamera *cam,
                                int screen_w, int screen_h,
                                float cursor_x, float cursor_y,
                                const vec3 center, const vec3 axis_n,
                                vec3 out_anchor) {
    vec3 ray_o, ray_d;
    safi_camera_screen_ray(cam, screen_w, screen_h, cursor_x, cursor_y,
                           ray_o, ray_d);
    vec3 hit;
    if (!ray_plane_intersect(ray_o, ray_d, center, axis_n, hit)) return false;
    glm_vec3_sub(hit, (float *)center, out_anchor);
    /* Strip any component along the axis — numerical safety if the ray's
     * hit is slightly off-plane. */
    float along = glm_vec3_dot(out_anchor, (float *)axis_n);
    out_anchor[0] -= axis_n[0] * along;
    out_anchor[1] -= axis_n[1] * along;
    out_anchor[2] -= axis_n[2] * along;
    if (glm_vec3_norm2(out_anchor) < 1e-8f) return false;
    glm_vec3_normalize(out_anchor);
    return true;
}

/* World-space handle length that keeps the gizmo ~constant pixels tall on
 * screen. `world_len ≈ distance * 2 tan(fov/2) * target_px / viewport_h` */
static float compute_handle_length(const SafiCamera *cam,
                                   const float target_pos[3],
                                   int viewport_h) {
    if (!cam || viewport_h <= 0) return 1.0f;

    float eye[3];
    if (cam->eye[0] != 0.0f || cam->eye[1] != 0.0f || cam->eye[2] != 0.0f) {
        eye[0] = cam->eye[0]; eye[1] = cam->eye[1]; eye[2] = cam->eye[2];
    } else {
        eye[0] = cam->target[0];
        eye[1] = cam->target[1];
        eye[2] = cam->target[2] + 3.0f;
    }

    float dx = target_pos[0] - eye[0];
    float dy = target_pos[1] - eye[1];
    float dz = target_pos[2] - eye[2];
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    if (dist < 0.01f) dist = 0.01f;

    const float target_px = 80.0f;
    float world_at_pixel = 2.0f * dist * tanf(cam->fov_y_radians * 0.5f) /
                           (float)viewport_h;
    return world_at_pixel * target_px;
}

static const SafiCamera *find_active_camera(ecs_world_t *world) {
    const SafiCamera *out = NULL;
    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { .id = ecs_id(SafiCamera) },
            { .id = ecs_id(SafiActiveCamera) },
        },
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        if (!out) {
            SafiCamera *cams = ecs_field(&it, SafiCamera, 0);
            out = &cams[0];
            ecs_iter_fini(&it);
            break;
        }
    }
    ecs_query_fini(q);
    return out;
}

/* Build the entity's parent-world matrix (identity for root entities or
 * parents without SafiGlobalTransform). Used to convert world-space drag
 * deltas into the entity's parent-local frame. */
static void fetch_parent_world(ecs_world_t *world, ecs_entity_t e,
                               mat4 out_parent_world) {
    glm_mat4_identity(out_parent_world);
    ecs_entity_t parent = ecs_get_target(world, e, EcsChildOf, 0);
    if (!parent) return;
    const SafiGlobalTransform *pgt = ecs_get(world, parent,
                                             SafiGlobalTransform);
    if (!pgt) return;
    glm_mat4_copy((vec4 *)pgt->matrix, out_parent_world);
}

/* -- Hit-test ------------------------------------------------------------ */

static int hit_test_axes(const SafiCamera *cam,
                         int screen_w, int screen_h,
                         const float origin_world[3],
                         float len,
                         float cursor_x, float cursor_y) {
    float ox, oy;
    if (!safi_camera_world_to_screen(cam, screen_w, screen_h,
                                     origin_world, &ox, &oy)) {
        return -1;
    }

    int best = -1;
    float best_d2 = HIT_THRESHOLD_PX * HIT_THRESHOLD_PX;

    for (int a = 0; a < 3; a++) {
        float end_world[3] = {
            origin_world[0] + AXIS_DIR[a][0] * len,
            origin_world[1] + AXIS_DIR[a][1] * len,
            origin_world[2] + AXIS_DIR[a][2] * len,
        };
        float ex, ey;
        if (!safi_camera_world_to_screen(cam, screen_w, screen_h,
                                         end_world, &ex, &ey)) {
            continue;
        }
        float d2 = pt_to_segment_sq(cursor_x, cursor_y, ox, oy, ex, ey);
        if (d2 < best_d2) {
            best_d2 = d2;
            best    = a;
        }
    }
    return best;
}

/* -- Ring hit-test ------------------------------------------------------- *
 *
 * Samples N points around each ring, projects them to screen, and finds
 * the segment closest to the cursor. Screen-space picking matches the
 * Unity "click the ring wherever it looks thickest on screen" behaviour
 * and side-steps the edge-on degenerate case where a ring is almost a
 * straight line. */

#define RING_HIT_SEGMENTS 24

static void ring_point(const float center[3], int axis, float radius,
                       float t, float out[3]) {
    float c = cosf(t) * radius;
    float s = sinf(t) * radius;
    switch (axis) {
    case 0: out[0] = center[0];       out[1] = center[1] + c; out[2] = center[2] + s; break;
    case 1: out[0] = center[0] + c;   out[1] = center[1];     out[2] = center[2] + s; break;
    default:out[0] = center[0] + c;   out[1] = center[1] + s; out[2] = center[2];     break;
    }
}

static int hit_test_rings(const SafiCamera *cam,
                          int screen_w, int screen_h,
                          const float center[3], float radius,
                          float cursor_x, float cursor_y) {
    int best = -1;
    float best_d2 = HIT_THRESHOLD_PX * HIT_THRESHOLD_PX;

    for (int a = 0; a < 3; a++) {
        float prev_x = 0.0f, prev_y = 0.0f;
        bool have_prev = false;

        for (int i = 0; i <= RING_HIT_SEGMENTS; i++) {
            float t = (float)(i % RING_HIT_SEGMENTS) /
                      (float)RING_HIT_SEGMENTS * 2.0f * (float)M_PI;
            float world_pt[3];
            ring_point(center, a, radius, t, world_pt);

            float sx, sy;
            if (!safi_camera_world_to_screen(cam, screen_w, screen_h,
                                             world_pt, &sx, &sy)) {
                have_prev = false;
                continue;
            }
            if (have_prev) {
                float d2 = pt_to_segment_sq(cursor_x, cursor_y,
                                            prev_x, prev_y, sx, sy);
                if (d2 < best_d2) {
                    best_d2 = d2;
                    best    = a;
                }
            }
            prev_x = sx; prev_y = sy; have_prev = true;
        }
    }
    return best;
}

/* -- Drag lifecycle ------------------------------------------------------ */

static void drag_begin(ecs_world_t *world, SafiEditorTool tool,
                       ecs_entity_t target, int axis,
                       const vec3 origin_world, const SafiCamera *cam,
                       int screen_w, int screen_h,
                       float cursor_x, float cursor_y) {
    vec3 ray_o, ray_d;
    safi_camera_screen_ray(cam, screen_w, screen_h, cursor_x, cursor_y,
                           ray_o, ray_d);

    vec3 axis_dir = { AXIS_DIR[axis][0], AXIS_DIR[axis][1], AXIS_DIR[axis][2] };

    /* Open a change-bus group so every per-frame transform write during
     * the drag coalesces into a single undo step for M6. */
    safi_change_bus_begin_group();

    g_drag.axis        = axis;
    g_drag.tool        = tool;
    g_drag.target      = target;
    glm_vec3_copy((float *)origin_world, g_drag.origin_world);
    glm_vec3_copy((float *)origin_world, g_drag.current_world);
    g_drag.start_param = closest_param_on_line2(ray_o, ray_d,
                                                origin_world, axis_dir);

    const SafiTransform *xf0 = ecs_get(world, target, SafiTransform);
    if (xf0) {
        glm_vec3_copy((float *)xf0->scale, g_drag.start_scale);
        glm_quat_copy((float *)xf0->rotation, g_drag.start_rotation);
    } else {
        g_drag.start_scale[0] = 1.0f;
        g_drag.start_scale[1] = 1.0f;
        g_drag.start_scale[2] = 1.0f;
        glm_quat_identity(g_drag.start_rotation);
    }

    /* Rotate: compute the anchor vector on the ring plane. Failure here
     * leaves rotate_anchor as a zero vec; drag_apply_rotate skips frames
     * until a valid sample lands (matches the ring click-and-drag UX —
     * you just re-anchor on the next sample rather than drift). */
    if (tool == SAFI_EDITOR_TOOL_ROTATE) {
        if (!compute_ring_anchor(cam, screen_w, screen_h, cursor_x, cursor_y,
                                 origin_world, axis_dir,
                                 g_drag.rotate_anchor)) {
            glm_vec3_zero(g_drag.rotate_anchor);
        }
    }

    mat4 parent_world;
    fetch_parent_world(world, target, parent_world);
    glm_mat4_inv(parent_world, g_drag.parent_inv);
}

static void drag_apply_translate(ecs_world_t *world, float delta_t,
                                 const vec3 axis_dir) {
    vec4 world_pt = {
        g_drag.origin_world[0] + axis_dir[0] * delta_t,
        g_drag.origin_world[1] + axis_dir[1] * delta_t,
        g_drag.origin_world[2] + axis_dir[2] * delta_t,
        1.0f,
    };
    vec4 local_pt;
    glm_mat4_mulv(g_drag.parent_inv, world_pt, local_pt);

    SafiTransform *xf = ecs_get_mut(world, g_drag.target, SafiTransform);
    if (!xf) return;
    xf->position[0] = local_pt[0];
    xf->position[1] = local_pt[1];
    xf->position[2] = local_pt[2];
    ecs_modified(world, g_drag.target, SafiTransform);

    /* Track the live world-space position so the gizmo handle can be
     * drawn at the entity's *new* pose this frame. Reading
     * SafiGlobalTransform directly would give us the previous frame's
     * value — transform propagation runs on EcsPostUpdate, after this
     * system. */
    g_drag.current_world[0] = world_pt[0];
    g_drag.current_world[1] = world_pt[1];
    g_drag.current_world[2] = world_pt[2];
}

static void drag_apply_rotate(ecs_world_t *world, const SafiCamera *cam,
                              int screen_w, int screen_h,
                              float cursor_x, float cursor_y) {
    /* Anchor failed to initialise on drag-start — try again. */
    if (glm_vec3_norm2(g_drag.rotate_anchor) < 1e-8f) {
        vec3 axis_dir = {
            AXIS_DIR[g_drag.axis][0],
            AXIS_DIR[g_drag.axis][1],
            AXIS_DIR[g_drag.axis][2],
        };
        compute_ring_anchor(cam, screen_w, screen_h, cursor_x, cursor_y,
                            g_drag.origin_world, axis_dir,
                            g_drag.rotate_anchor);
        return;
    }

    vec3 axis_dir = {
        AXIS_DIR[g_drag.axis][0],
        AXIS_DIR[g_drag.axis][1],
        AXIS_DIR[g_drag.axis][2],
    };
    vec3 curr;
    if (!compute_ring_anchor(cam, screen_w, screen_h, cursor_x, cursor_y,
                             g_drag.origin_world, axis_dir, curr)) {
        return;
    }

    /* Signed angle from the drag-start anchor to the current anchor, around
     * the axis. dot → cos(θ); (anchor × curr) · axis → sin(θ). atan2 gives
     * the signed result directly. */
    float cos_t = glm_vec3_dot(g_drag.rotate_anchor, curr);
    vec3 cr;
    glm_vec3_cross(g_drag.rotate_anchor, curr, cr);
    float sin_t = glm_vec3_dot(cr, axis_dir);
    float angle = atan2f(sin_t, cos_t);

    /* Compose: world-space new rotation = delta * start. For unparented
     * entities world and local match, so the quaternion is written back
     * directly. Parented entities fall through the same path (the plan
     * flagged this as a simplification — good enough while M4 is about
     * feel, not rigour). */
    versor delta, new_rot;
    glm_quatv(delta, angle, axis_dir);
    glm_quat_mul(delta, g_drag.start_rotation, new_rot);
    glm_quat_normalize(new_rot);

    SafiTransform *xf = ecs_get_mut(world, g_drag.target, SafiTransform);
    if (!xf) return;
    glm_quat_copy(new_rot, xf->rotation);
    ecs_modified(world, g_drag.target, SafiTransform);
}

static void drag_apply_scale(ecs_world_t *world, float delta_t,
                             float handle_len) {
    /* Map world-space delta into a multiplicative factor. Normalising by
     * the handle length means "drag the cube to twice its resting
     * distance" always feels like "double the scale" regardless of how
     * far the camera is. */
    if (handle_len < 1e-4f) return;
    float factor = 1.0f + delta_t / handle_len;
    if (factor < 0.01f) factor = 0.01f;

    SafiTransform *xf = ecs_get_mut(world, g_drag.target, SafiTransform);
    if (!xf) return;
    float v = g_drag.start_scale[g_drag.axis] * factor;
    if (v < SCALE_MIN) v = SCALE_MIN;
    xf->scale[g_drag.axis] = v;
    ecs_modified(world, g_drag.target, SafiTransform);
}

static void drag_update(ecs_world_t *world, const SafiCamera *cam,
                        int screen_w, int screen_h,
                        float cursor_x, float cursor_y,
                        float handle_len) {
    if (!ecs_is_alive(world, g_drag.target)) {
        g_drag.axis = -1;
        return;
    }

    if (g_drag.tool == SAFI_EDITOR_TOOL_ROTATE) {
        drag_apply_rotate(world, cam, screen_w, screen_h,
                          cursor_x, cursor_y);
        return;
    }

    vec3 ray_o, ray_d;
    safi_camera_screen_ray(cam, screen_w, screen_h, cursor_x, cursor_y,
                           ray_o, ray_d);

    vec3 axis_dir = {
        AXIS_DIR[g_drag.axis][0],
        AXIS_DIR[g_drag.axis][1],
        AXIS_DIR[g_drag.axis][2],
    };

    float t       = closest_param_on_line2(ray_o, ray_d,
                                           g_drag.origin_world, axis_dir);
    float delta_t = t - g_drag.start_param;

    switch (g_drag.tool) {
    case SAFI_EDITOR_TOOL_TRANSLATE:
        drag_apply_translate(world, delta_t, axis_dir);
        break;
    case SAFI_EDITOR_TOOL_SCALE:
        drag_apply_scale(world, delta_t, handle_len);
        break;
    default:
        break;
    }
}

static void drag_end(void) {
    g_drag.axis = -1;
    safi_change_bus_end_group();
}

/* -- Draw helpers -------------------------------------------------------- */

static void draw_translate(const float pos[3], float len, int active_axis) {
    float tip_half = len * 0.08f;
    for (int a = 0; a < 3; a++) {
        const float *color = (a == active_axis) ? HIGHLIGHT_COLOR
                                                 : AXIS_COLOR[a];
        float end[3] = {
            pos[0] + AXIS_DIR[a][0] * len,
            pos[1] + AXIS_DIR[a][1] * len,
            pos[2] + AXIS_DIR[a][2] * len,
        };
        safi_gizmo_draw_line(pos, end, color);
        float half[3] = { tip_half, tip_half, tip_half };
        safi_gizmo_draw_box_wire(end, half, color);
    }
}

static void draw_rotate(const float pos[3], float radius, int active_axis) {
    const int segments = 48;
    for (int a = 0; a < 3; a++) {
        const float *color = (a == active_axis) ? HIGHLIGHT_COLOR
                                                 : AXIS_COLOR[a];
        float prev[3];
        ring_point(pos, a, radius, 0.0f, prev);
        for (int i = 1; i <= segments; i++) {
            float t = (float)i / (float)segments * 2.0f * (float)M_PI;
            float cur[3];
            ring_point(pos, a, radius, t, cur);
            safi_gizmo_draw_line(prev, cur, color);
            prev[0] = cur[0]; prev[1] = cur[1]; prev[2] = cur[2];
        }
    }

    /* Small centre cube so the pivot is visible at a glance even when the
     * rings project edge-on. */
    float half = radius * 0.04f;
    float centre_half[3] = { half, half, half };
    const float centre_color[4] = { 0.85f, 0.85f, 0.85f, 1.0f };
    safi_gizmo_draw_box_wire(pos, centre_half, centre_color);
}

static void draw_scale(const float pos[3], float len, int active_axis) {
    /* Fatter cubes at the tips and a small cube at the centre — matches
     * the Unity/Unreal visual language for scale vs. translate. */
    float tip_half = len * 0.13f;
    float origin_half = len * 0.08f;

    for (int a = 0; a < 3; a++) {
        const float *color = (a == active_axis) ? HIGHLIGHT_COLOR
                                                 : AXIS_COLOR[a];
        float end[3] = {
            pos[0] + AXIS_DIR[a][0] * len,
            pos[1] + AXIS_DIR[a][1] * len,
            pos[2] + AXIS_DIR[a][2] * len,
        };
        safi_gizmo_draw_line(pos, end, color);
        float half[3] = { tip_half, tip_half, tip_half };
        safi_gizmo_draw_box_wire(end, half, color);
    }

    float centre_half[3] = { origin_half, origin_half, origin_half };
    const float centre_color[4] = { 0.85f, 0.85f, 0.85f, 1.0f };
    safi_gizmo_draw_box_wire(pos, centre_half, centre_color);
}

/* -- System -------------------------------------------------------------- */

static void editor_gizmo_system(ecs_iter_t *it) {
    g_hovered_axis = -1;

    if (safi_editor_get_mode(it->world) != SAFI_EDITOR_MODE_EDIT) {
        if (g_drag.axis >= 0) drag_end();
        return;
    }

    SafiEditorTool tool = safi_editor_get_tool(it->world);
    if (tool == SAFI_EDITOR_TOOL_SELECT) {
        if (g_drag.axis >= 0) drag_end();
        return;
    }

    ecs_entity_t sel = safi_editor_get_selected(it->world);
    if (!sel || !ecs_is_alive(it->world, sel)) {
        if (g_drag.axis >= 0) drag_end();
        return;
    }

    const SafiGlobalTransform *gt = ecs_get(it->world, sel,
                                            SafiGlobalTransform);
    if (!gt) return;

    /* Pull world translation out of the 4x4 (column-major). During a
     * translate drag the entity's position changes each frame, so prefer
     * the drag's live world position — SafiGlobalTransform is still
     * yesterday's value at this point (transform propagation runs later
     * on EcsPostUpdate). Rotate / scale don't move the origin, so reading
     * SafiGlobalTransform is fine for those. */
    float pos[3];
    if (g_drag.axis >= 0 && g_drag.target == sel) {
        pos[0] = g_drag.current_world[0];
        pos[1] = g_drag.current_world[1];
        pos[2] = g_drag.current_world[2];
    } else {
        pos[0] = gt->matrix[3][0];
        pos[1] = gt->matrix[3][1];
        pos[2] = gt->matrix[3][2];
    }

    const SafiCamera *cam = find_active_camera(it->world);
    if (!cam) return;

    int ww = 0, wh = 0;
    SDL_Window *win = SDL_GetKeyboardFocus();
    if (win) SDL_GetWindowSize(win, &ww, &wh);
    if (wh <= 0) return;

    float len = compute_handle_length(cam, pos, wh);

    const SafiInput *in = ecs_singleton_get(it->world, SafiInput);
    bool lmb = (in && in->mouse_buttons[SDL_BUTTON_LEFT]);
    bool manipulable = (tool == SAFI_EDITOR_TOOL_TRANSLATE) ||
                       (tool == SAFI_EDITOR_TOOL_SCALE) ||
                       (tool == SAFI_EDITOR_TOOL_ROTATE);

    if (g_drag.axis >= 0 && !lmb) drag_end();

    if (in && manipulable && g_drag.axis < 0 &&
        safi_debug_ui_mouse_over_viewport()) {
        if (tool == SAFI_EDITOR_TOOL_ROTATE) {
            g_hovered_axis = hit_test_rings(cam, ww, wh, pos, len,
                                            in->mouse_x, in->mouse_y);
        } else {
            g_hovered_axis = hit_test_axes(cam, ww, wh, pos, len,
                                           in->mouse_x, in->mouse_y);
        }
    }

    if (in && lmb && g_drag.axis < 0 && g_hovered_axis >= 0 &&
        manipulable && safi_debug_ui_mouse_over_viewport()) {
        vec3 origin = { pos[0], pos[1], pos[2] };
        drag_begin(it->world, tool, sel, g_hovered_axis, origin,
                   cam, ww, wh, in->mouse_x, in->mouse_y);
    }

    if (in && g_drag.axis >= 0 && lmb) {
        drag_update(it->world, cam, ww, wh, in->mouse_x, in->mouse_y, len);
    }

    int active_axis = (g_drag.axis >= 0) ? g_drag.axis : g_hovered_axis;

    switch (tool) {
    case SAFI_EDITOR_TOOL_TRANSLATE:
        draw_translate(pos, len, active_axis);
        break;
    case SAFI_EDITOR_TOOL_SCALE:
        draw_scale(pos, len, active_axis);
        break;
    case SAFI_EDITOR_TOOL_ROTATE:
        draw_rotate(pos, len, active_axis);
        break;
    default:
        break;
    }
}

void safi_editor_gizmo_install(ecs_world_t *world) {
    if (!world) return;
    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "safi_editor_gizmo_system",
            .add  = ecs_ids(ecs_dependson(EcsOnUpdate)),
        }),
        .callback = editor_gizmo_system,
    });
}
