/**
 * safi/render/shader.h — shader creation.
 *
 * Shaders are authored once in HLSL. The CMake build compiles each .hlsl
 * into a pair of per-stage artifacts (<name>.<stage>.spv and .msl) via
 * glslangValidator + spirv-cross. At runtime, safi_shader_load() inspects
 * SDL_GetGPUShaderFormats() and opens the artifact matching the active
 * backend — the caller only knows the logical shader name.
 *
 * Backends covered today:
 *   - Metal    → .msl
 *   - Vulkan   → .spv
 *   - D3D12    → .spv (works as a first pass; DXIL comes later once a
 *                       Windows CI host can run DXC)
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

/* Low-level: create a GPU shader directly from a pre-compiled byte blob of
 * a known format. The runtime loader is built on top of this. Prefer
 * safi_shader_load() unless you are shipping embedded bytecode.
 *
 * `num_*` must match the resource declarations in the source. See the
 * SDL_GPU documentation for how these map to Metal/Vulkan/D3D12 slots. */
SDL_GPUShader *safi_shader_create_blob(SafiRenderer      *r,
                                       const void        *bytes,
                                       size_t             size,
                                       SDL_GPUShaderFormat format,
                                       const char        *entrypoint,
                                       SafiShaderStage    stage,
                                       uint32_t           num_samplers,
                                       uint32_t           num_uniform_buffers,
                                       uint32_t           num_storage_buffers,
                                       uint32_t           num_storage_textures);

/* High-level: load a compiled shader by logical name + stage. The loader
 * picks the artifact extension (.spv / .msl) that matches the device's
 * supported shader formats, reads the file from
 *   <shader_dir>/<name>.<vert|frag>.<ext>
 * and hands it to safi_shader_create_blob(). Returns NULL on failure. */
SDL_GPUShader *safi_shader_load(SafiRenderer    *r,
                                const char      *shader_dir,
                                const char      *name,
                                const char      *entrypoint,
                                SafiShaderStage  stage,
                                uint32_t         num_samplers,
                                uint32_t         num_uniform_buffers,
                                uint32_t         num_storage_buffers,
                                uint32_t         num_storage_textures);

#endif /* SAFI_RENDER_SHADER_H */
