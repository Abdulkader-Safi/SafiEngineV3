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

#include "safi/input/input.h"

#include "safi/render/renderer.h"
#include "safi/render/mesh.h"
#include "safi/render/material.h"
#include "safi/render/shader.h"
#include "safi/render/gltf_loader.h"
#include "safi/render/model.h"

#include "safi/ui/debug_ui.h"

#endif /* SAFI_H */
