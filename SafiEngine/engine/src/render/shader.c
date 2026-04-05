#include "safi/render/shader.h"
#include "safi/core/log.h"

#include <SDL3/SDL.h>
#include <string.h>

SDL_GPUShader *safi_shader_create(SafiRenderer *r, const char *source,
                                  size_t source_length, const char *entrypoint,
                                  SafiShaderStage stage, uint32_t num_samplers,
                                  uint32_t num_uniform_buffers,
                                  uint32_t num_storage_buffers,
                                  uint32_t num_storage_textures) {
  if (source_length == 0 && source)
    source_length = strlen(source);

  SDL_GPUShaderCreateInfo info = {
      .code_size = source_length,
      .code = (const Uint8 *)source,
      .entrypoint = entrypoint,
      .format = SDL_GPU_SHADERFORMAT_MSL,
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
