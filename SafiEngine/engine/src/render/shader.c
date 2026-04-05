#include "safi/render/shader.h"
#include "safi/core/log.h"

#include <SDL3/SDL.h>

SDL_GPUShader *safi_shader_create_blob(SafiRenderer      *r,
                                       const void        *bytes,
                                       size_t             size,
                                       SDL_GPUShaderFormat format,
                                       const char        *entrypoint,
                                       SafiShaderStage    stage,
                                       uint32_t           num_samplers,
                                       uint32_t           num_uniform_buffers,
                                       uint32_t           num_storage_buffers,
                                       uint32_t           num_storage_textures) {
  SDL_GPUShaderCreateInfo info = {
      .code_size = size,
      .code = (const Uint8 *)bytes,
      .entrypoint = entrypoint,
      .format = format,
      .stage = (stage == SAFI_SHADER_STAGE_VERTEX)
                   ? SDL_GPU_SHADERSTAGE_VERTEX
                   : SDL_GPU_SHADERSTAGE_FRAGMENT,
      .num_samplers = num_samplers,
      .num_storage_textures = num_storage_textures,
      .num_storage_buffers = num_storage_buffers,
      .num_uniform_buffers = num_uniform_buffers,
  };

  SDL_GPUShader *sh = SDL_CreateGPUShader(r->device, &info);
  if (!sh) {
    SAFI_LOG_ERROR("SDL_CreateGPUShader('%s') failed: %s", entrypoint,
                   SDL_GetError());
  }
  return sh;
}
