/**
 * safi/render/shader.h — HLSL → backend-native via SDL_shadercross.
 *
 * The engine ships shaders as HLSL source and compiles them at startup
 * using SDL_shadercross, which produces Metal MSL / Vulkan SPIR-V / D3D12
 * DXIL depending on the active SDL_gpu backend. No offline toolchain is
 * required on the user's machine.
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

/* Compile HLSL source for the current backend. `entrypoint` usually is
 * "vs_main" or "fs_main". Returns NULL on failure; check SDL_GetError(). */
SDL_GPUShader *safi_shader_compile_hlsl(SafiRenderer       *r,
                                        const char         *hlsl_source,
                                        size_t              hlsl_length,
                                        const char         *entrypoint,
                                        SafiShaderStage     stage,
                                        uint32_t            num_samplers,
                                        uint32_t            num_uniform_buffers,
                                        uint32_t            num_storage_buffers,
                                        uint32_t            num_storage_textures);

#endif /* SAFI_RENDER_SHADER_H */
