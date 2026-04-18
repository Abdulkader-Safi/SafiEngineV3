/*
 * control_system.h — Keyboard-driven gameplay and scene IO for the demo.
 *
 * control_system runs on SafiGamePhase — so WASD/arrow input drives the
 * model only while the app is in Play mode. In Edit mode the editor fly-cam
 * owns those keys. Skips input when a MicroUI widget has focus so typing
 * into the Inspector doesn't move the scene.
 *
 * scene_io_system runs on EcsOnUpdate (always ticks) so F5 save / F9 load
 * work from Edit mode too. F9 refreshes the demo's cached entity handles
 * by name after the scene is reloaded.
 */
#ifndef GLTF_VIEWER_CONTROL_SYSTEM_H
#define GLTF_VIEWER_CONTROL_SYSTEM_H

#include <safi/safi.h>

void control_system (ecs_iter_t *it);
void scene_io_system(ecs_iter_t *it);

#endif /* GLTF_VIEWER_CONTROL_SYSTEM_H */
