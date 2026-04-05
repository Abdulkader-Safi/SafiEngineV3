#include "safi/render/shader.h"
#include "safi/core/log.h"

#include <SDL3_shadercross/SDL_shadercross.h>
#include <string.h>

SDL_GPUShader *safi_shader_compile_hlsl(SafiRenderer   *r,
                                        const char     *hlsl_source,
                                        size_t          hlsl_length,
                                        const char     *entrypoint,
                                        SafiShaderStage stage,
                                        uint32_t        num_samplers,
                                        uint32_t        num_uniform_buffers,
                                        uint32_t        num_storage_buffers,
                                        uint32_t        num_storage_textures) {
    /* Initialise shadercross once per process. Safe to call repeatedly. */
    static bool initialised = false;
    if (!initialised) {
        if (!SDL_ShaderCross_Init()) {
            SAFI_LOG_ERROR("SDL_ShaderCross_Init failed: %s", SDL_GetError());
            return NULL;
        }
        initialised = true;
    }

    SDL_ShaderCross_HLSL_Info hlsl_info = {
        .source            = hlsl_source,
        .entrypoint        = entrypoint,
        .include_dir       = NULL,
        .defines           = NULL,
        .shader_stage      = (stage == SAFI_SHADER_STAGE_VERTEX)
                                 ? SDL_SHADERCROSS_SHADERSTAGE_VERTEX
                                 : SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
        .enable_debug      = true,
        .name              = entrypoint,
        .props             = 0,
    };
    (void)hlsl_length;

    SDL_ShaderCross_GraphicsShaderMetadata metadata = {
        .num_samplers         = num_samplers,
        .num_storage_textures = num_storage_textures,
        .num_storage_buffers  = num_storage_buffers,
        .num_uniform_buffers  = num_uniform_buffers,
    };

    SDL_GPUShader *shader = SDL_ShaderCross_CompileGraphicsShaderFromHLSL(
        r->device, &hlsl_info, &metadata, 0);
    if (!shader) {
        SAFI_LOG_ERROR("SDL_ShaderCross compile failed: %s", SDL_GetError());
    }
    return shader;
}
