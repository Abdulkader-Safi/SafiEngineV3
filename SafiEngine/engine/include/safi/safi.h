/**
 * safi.h — umbrella header for SafiEngine.
 *
 * Include this to pull in every public API of the engine. Individual
 * subsystems can also be included directly from <safi/...>.
 */
#ifndef SAFI_H
#define SAFI_H

#include "safi/core/app.h"
#include "safi/core/log.h"
#include "safi/core/time.h"

#include "safi/ecs/ecs.h"
#include "safi/ecs/components.h"
#include "safi/ecs/phases.h"
#include "safi/ecs/transform.h"

#include "safi/input/input.h"

#include "safi/render/renderer.h"
#include "safi/render/assets.h"
#include "safi/render/mesh.h"
#include "safi/render/material.h"
#include "safi/render/shader.h"
#include "safi/render/gltf_loader.h"
#include "safi/render/model.h"
#include "safi/render/primitive_mesh.h"
#include "safi/render/primitive_system.h"
#include "safi/render/light_buffer.h"
#include "safi/render/light_system.h"
#include "safi/render/render_system.h"
#include "safi/render/camera.h"
#include "safi/render/gizmo.h"

#include "safi/scene/scene.h"
#include "safi/ecs/component_registry.h"

#include "safi/editor/editor_state.h"
#include "safi/editor/editor_camera.h"
#include "safi/editor/editor_shortcuts.h"
#include "safi/editor/editor_toolbar.h"
#include "safi/editor/editor_gizmo.h"

#include "safi/physics/physics.h"

#include "safi/audio/audio.h"

#include "safi/ui/debug_ui.h"

#endif /* SAFI_H */
