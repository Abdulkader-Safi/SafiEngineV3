/**
 * safi/render/shader.h — shader creation.
 *
 * First-milestone note: the engine currently ships Metal Shading Language
 * (MSL) source and loads it directly via SDL_CreateGPUShader. Cross-platform
 * shader compilation (via SDL_shadercross or offline glslang) is a planned
 * follow-up. The public function signature is intentionally backend-agnostic
 * so that switch happens without touching callers.
 */
#ifndef SAFI_RENDER_SHADER_H
#define SAFI_RENDER_SHADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <SDL3/SDL_gpu.h>

#include "safi/render/renderer.h"

typedef enum SafiShaderStage {
    SAFI_SHADER_STAGE_VERTEX,
    SAFI_SHADER_STAGE_FRAGMENT,
} SafiShaderStage;

/* Create a GPU shader from source. Currently expects MSL for the Metal
 * backend; when cross-compilation lands this will transparently accept HLSL
 * or GLSL as well. Returns NULL on failure; call SDL_GetError() for details.
 *
 * `num_*` values describe the shader's resource bindings — they must match
 * the actual declarations in the source. See the SDL_gpu Metal binding
 * layout docs for how these map to [[buffer]] / [[texture]] / [[sampler]]
 * slot indices. */
SDL_GPUShader *safi_shader_create(SafiRenderer    *r,
                                  const char      *source,
                                  size_t           source_length,
                                  const char      *entrypoint,
                                  SafiShaderStage  stage,
                                  uint32_t         num_samplers,
                                  uint32_t         num_uniform_buffers,
                                  uint32_t         num_storage_buffers,
                                  uint32_t         num_storage_textures);

#endif /* SAFI_RENDER_SHADER_H */
