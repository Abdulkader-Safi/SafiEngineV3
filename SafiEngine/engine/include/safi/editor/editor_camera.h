/**
 * safi/editor/editor_camera.h — editor-owned fly-cam for Edit mode.
 *
 * A dedicated ECS entity carries `{SafiTransform, SafiCamera,
 * SafiEditorCamera}` and is activated whenever the editor is in Edit mode.
 * While active, the cursor drives a fly-cam: right-mouse-drag rotates yaw
 * and pitch; WASD/QE translate along forward / right / world-up; Shift
 * multiplies speed, Ctrl divides.
 *
 * The gameplay camera (whatever entity holds `SafiActiveCamera` when the
 * editor cam is installed, or gains it later) is restored automatically
 * on the switch back to Play / Paused — the editor cam just borrows the
 * active-camera tag while Edit mode is live.
 */
#ifndef SAFI_EDITOR_EDITOR_CAMERA_H
#define SAFI_EDITOR_EDITOR_CAMERA_H

#include <stdbool.h>
#include <flecs.h>

typedef struct SafiEditorCamera {
    float yaw;                   /* radians, rotation about world +Y        */
    float pitch;                 /* radians, clamped ±89°                   */
    float move_speed;            /* units / second (base)                   */
    float look_speed;            /* radians / pixel of mouse motion         */
    bool  dragging;              /* true while RMB is held over the view    */

    /* Private: last entity that held SafiActiveCamera before the editor cam
     * stole the tag. Restored to it on the switch back to Play. 0 when the
     * editor cam has not taken the tag yet, or when there was nothing to
     * remember. */
    ecs_entity_t _prev_active_cam;
} SafiEditorCamera;

/* Install the editor camera on `world`: creates (or finds) the singleton
 * camera entity with `{SafiTransform, SafiCamera, SafiEditorCamera}`,
 * registers the fly-cam system on `EcsOnUpdate`, and primes the
 * active-camera arbitration. Idempotent — safe to call more than once.
 *
 * Called automatically by `safi_app_init` when `enable_debug_ui` is set.
 * Apps that bring their own world can call it manually. */
void safi_editor_camera_install(ecs_world_t *world);

/* Return the editor cam entity (0 if not installed yet). */
ecs_entity_t safi_editor_camera_entity(const ecs_world_t *world);

extern ECS_COMPONENT_DECLARE(SafiEditorCamera);

#endif /* SAFI_EDITOR_EDITOR_CAMERA_H */
