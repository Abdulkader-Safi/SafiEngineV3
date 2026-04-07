/**
 * safi/render/light_system.h — collect ECS light entities into a GPU buffer.
 *
 * Call safi_light_buffer_collect() once per frame to gather all light
 * components from the ECS world and pack them into a SafiLightBuffer
 * ready for upload to the GPU.
 */
#ifndef SAFI_RENDER_LIGHT_SYSTEM_H
#define SAFI_RENDER_LIGHT_SYSTEM_H

#include <flecs.h>
#include "safi/render/light_buffer.h"

/* Query all light entities in the world and pack them into `out`.
 * Sky lights set the ambient term; all others go into the lights array.
 * At most SAFI_MAX_LIGHTS non-ambient lights are collected. */
void safi_light_buffer_collect(ecs_world_t *world, SafiLightBuffer *out);

#endif /* SAFI_RENDER_LIGHT_SYSTEM_H */
