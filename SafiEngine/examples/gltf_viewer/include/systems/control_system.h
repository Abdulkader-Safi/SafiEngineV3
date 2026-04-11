/*
 * control_system.h — Keyboard-driven transform and camera control.
 *
 * Reads the SafiInput singleton each OnUpdate tick and:
 *   - rotates the model entity (arrows = yaw/pitch, A/D = roll)
 *   - dollies the camera along -Z / +Z (W/S)
 *
 * Skips input when a MicroUI widget has focus so typing in the inspector
 * doesn't move the scene.
 */
#ifndef GLTF_VIEWER_CONTROL_SYSTEM_H
#define GLTF_VIEWER_CONTROL_SYSTEM_H

#include <safi/safi.h>

void control_system(ecs_iter_t *it);

#endif /* GLTF_VIEWER_CONTROL_SYSTEM_H */
