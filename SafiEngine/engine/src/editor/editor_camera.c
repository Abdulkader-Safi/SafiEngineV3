#include "safi/editor/editor_camera.h"
#include "safi/editor/editor_state.h"
#include "safi/ecs/components.h"
#include "safi/input/input.h"
#include "safi/ui/debug_ui.h"

#include <SDL3/SDL.h>
#include <cglm/cglm.h>
#include <math.h>
#include <string.h>

ECS_COMPONENT_DECLARE(SafiEditorCamera);

/* Cached editor-cam entity. Singleton by construction — install() refuses to
 * create a second one. Zero until the first install() call. */
static ecs_entity_t g_editor_cam = 0;

/* Clamp pitch just shy of ±90° so gimbal lock doesn't collapse the view. */
static const float PITCH_LIMIT = 1.55334f; /* ~89° in radians */

static void compute_basis(float yaw, float pitch, vec3 out_forward,
                          vec3 out_up) {
    float cp = cosf(pitch);
    float sp = sinf(pitch);
    float cy = cosf(yaw);
    float sy = sinf(yaw);

    /* Right-handed: yaw 0, pitch 0 looks down -Z. */
    out_forward[0] = -sy * cp;
    out_forward[1] =  sp;
    out_forward[2] = -cy * cp;

    vec3 world_up = {0.0f, 1.0f, 0.0f};
    vec3 right;
    glm_vec3_cross(out_forward, world_up, right);
    if (glm_vec3_norm2(right) < 1e-8f) {
        /* Looking straight up / down — pick any right vector. */
        right[0] = 1.0f; right[1] = 0.0f; right[2] = 0.0f;
    }
    glm_vec3_normalize(right);
    glm_vec3_cross(right, out_forward, out_up);
    glm_vec3_normalize(out_up);
}

/* Swap the SafiActiveCamera tag to `cam` if it isn't already there. Returns
 * the entity that held the tag before the swap (0 if none). */
static ecs_entity_t grab_active(ecs_world_t *world, ecs_entity_t cam) {
    ecs_entity_t prev = 0;
    ecs_query_t *q = ecs_query(world, {
        .terms = {{ .id = ecs_id(SafiActiveCamera) }},
        .cache_kind = EcsQueryCacheNone,
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        for (int i = 0; i < it.count; i++) {
            if (it.entities[i] != cam) {
                prev = it.entities[i];
                ecs_remove(world, it.entities[i], SafiActiveCamera);
                ecs_iter_fini(&it);
                goto done;
            }
        }
    }
done:
    ecs_query_fini(q);
    if (!ecs_has(world, cam, SafiActiveCamera)) {
        ecs_add(world, cam, SafiActiveCamera);
    }
    return prev;
}

/* Move the SafiActiveCamera tag back to `target` (if alive) and remove it
 * from `cam`. Called when mode leaves EDIT. */
static void release_active(ecs_world_t *world, ecs_entity_t cam,
                           ecs_entity_t target) {
    if (ecs_has(world, cam, SafiActiveCamera)) {
        ecs_remove(world, cam, SafiActiveCamera);
    }
    if (target && ecs_is_alive(world, target)) {
        if (!ecs_has(world, target, SafiActiveCamera)) {
            ecs_add(world, target, SafiActiveCamera);
        }
    }
}

static void editor_camera_system(ecs_iter_t *it) {
    if (!g_editor_cam || !ecs_is_alive(it->world, g_editor_cam)) return;

    SafiEditorCamera *ec = ecs_get_mut(it->world, g_editor_cam,
                                      SafiEditorCamera);
    SafiCamera       *cam = ecs_get_mut(it->world, g_editor_cam, SafiCamera);
    SafiTransform    *xf  = ecs_get_mut(it->world, g_editor_cam,
                                        SafiTransform);
    if (!ec || !cam || !xf) return;

    SafiEditorMode mode = safi_editor_get_mode(it->world);

    /* Arbitrate the SafiActiveCamera tag at the top of every frame.
     * Putting this in the system (instead of an observer) keeps the policy
     * in one place and side-steps the need to care about observer ordering. */
    if (mode == SAFI_EDITOR_MODE_EDIT) {
        if (!ecs_has(it->world, g_editor_cam, SafiActiveCamera)) {
            ec->_prev_active_cam = grab_active(it->world, g_editor_cam);
        }
    } else {
        if (ecs_has(it->world, g_editor_cam, SafiActiveCamera)) {
            release_active(it->world, g_editor_cam, ec->_prev_active_cam);
            ec->_prev_active_cam = 0;
            /* Drop any active drag when Play starts — otherwise relative
             * mouse mode could linger into gameplay. */
            if (ec->dragging) {
                SDL_Window *w = SDL_GetKeyboardFocus();
                if (w) SDL_SetWindowRelativeMouseMode(w, false);
                ec->dragging = false;
            }
        }
        ecs_modified(it->world, g_editor_cam, SafiEditorCamera);
        return; /* nothing to steer in Play / Paused */
    }

    const SafiInput *in = ecs_singleton_get(it->world, SafiInput);
    if (!in) return;

    float dt = it->delta_time;
    bool rmb = in->mouse_buttons[SDL_BUTTON_RIGHT];

    /* Only accept a NEW RMB press if the cursor is over the viewport. Once
     * dragging, track through panels so the user can sweep the cursor
     * freely while rotating. */
    if (rmb && !ec->dragging && safi_debug_ui_mouse_over_viewport()) {
        ec->dragging = true;
        SDL_Window *w = SDL_GetKeyboardFocus();
        if (w) SDL_SetWindowRelativeMouseMode(w, true);
    } else if (!rmb && ec->dragging) {
        ec->dragging = false;
        SDL_Window *w = SDL_GetKeyboardFocus();
        if (w) SDL_SetWindowRelativeMouseMode(w, false);
    }

    if (ec->dragging && (in->mouse_dx != 0.0f || in->mouse_dy != 0.0f)) {
        ec->yaw   -= in->mouse_dx * ec->look_speed;
        ec->pitch -= in->mouse_dy * ec->look_speed;
        if (ec->pitch >  PITCH_LIMIT) ec->pitch =  PITCH_LIMIT;
        if (ec->pitch < -PITCH_LIMIT) ec->pitch = -PITCH_LIMIT;
    }

    vec3 forward, up;
    compute_basis(ec->yaw, ec->pitch, forward, up);
    vec3 world_up = {0.0f, 1.0f, 0.0f};
    vec3 right;
    glm_vec3_cross(forward, world_up, right);
    glm_vec3_normalize(right);

    /* WASD/QE translate only when the keyboard isn't captured by a widget
     * (text field etc.). Without this guard, typing into the Inspector
     * would fly the camera. */
    if (!safi_debug_ui_wants_input()) {
        float speed = ec->move_speed;
        if (in->modifiers & SDL_KMOD_SHIFT) speed *= 4.0f;
        if (in->modifiers & SDL_KMOD_CTRL)  speed *= 0.25f;

        vec3 move = {0, 0, 0};
        if (in->keys[SDL_SCANCODE_W]) glm_vec3_add(move, forward, move);
        if (in->keys[SDL_SCANCODE_S]) glm_vec3_sub(move, forward, move);
        if (in->keys[SDL_SCANCODE_D]) glm_vec3_add(move, right,   move);
        if (in->keys[SDL_SCANCODE_A]) glm_vec3_sub(move, right,   move);
        if (in->keys[SDL_SCANCODE_E]) glm_vec3_add(move, world_up, move);
        if (in->keys[SDL_SCANCODE_Q]) glm_vec3_sub(move, world_up, move);

        if (glm_vec3_norm2(move) > 0.0f) {
            glm_vec3_normalize(move);
            glm_vec3_scale(move, speed * dt, move);
            glm_vec3_add(cam->eye, move, cam->eye);
        }
    }

    glm_vec3_copy(forward, cam->forward);
    glm_vec3_copy(up,      cam->up);
    glm_vec3_copy(cam->eye, xf->position);

    ecs_modified(it->world, g_editor_cam, SafiCamera);
    ecs_modified(it->world, g_editor_cam, SafiTransform);
    ecs_modified(it->world, g_editor_cam, SafiEditorCamera);
}

ecs_entity_t safi_editor_camera_entity(const ecs_world_t *world) {
    (void)world;
    return g_editor_cam;
}

void safi_editor_camera_install(ecs_world_t *world) {
    if (!world) return;
    if (g_editor_cam && ecs_is_alive(world, g_editor_cam)) return;

    g_editor_cam = ecs_new(world);
    ecs_set(world, g_editor_cam, SafiName, { .value = "EditorCamera" });

    SafiTransform xf = {0};
    xf.position[0] = 0.0f; xf.position[1] = 1.5f; xf.position[2] = 6.0f;
    xf.rotation[3] = 1.0f;
    xf.scale[0] = xf.scale[1] = xf.scale[2] = 1.0f;
    ecs_set_ptr(world, g_editor_cam, SafiTransform, &xf);

    SafiCamera cam = {0};
    cam.fov_y_radians = 1.0472f; /* 60° */
    cam.z_near = 0.1f;
    cam.z_far  = 1000.0f;
    cam.eye[0] = xf.position[0];
    cam.eye[1] = xf.position[1];
    cam.eye[2] = xf.position[2];
    cam.forward[0] = 0.0f; cam.forward[1] = 0.0f; cam.forward[2] = -1.0f;
    cam.up[0]      = 0.0f; cam.up[1]      = 1.0f; cam.up[2]      = 0.0f;
    ecs_set_ptr(world, g_editor_cam, SafiCamera, &cam);

    SafiEditorCamera ec = {
        .yaw          = 0.0f,
        .pitch        = 0.0f,
        .move_speed   = 5.0f,
        .look_speed   = 0.003f,
        .dragging     = false,
        ._prev_active_cam = 0,
    };
    ecs_set_ptr(world, g_editor_cam, SafiEditorCamera, &ec);

    /* Runs on EcsOnUpdate (variable phase, always ticks) so the fly-cam
     * works in Edit *and* Paused. The gate at the top of the callback
     * returns early outside Edit, keeping the active-cam swap cheap. */
    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "safi_editor_camera_system",
            .add  = ecs_ids(ecs_dependson(EcsOnUpdate)),
        }),
        .callback = editor_camera_system,
    });
}
