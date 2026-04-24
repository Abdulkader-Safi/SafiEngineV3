/*
 * shader_load.c — runtime file-based shader loader.
 *
 * Picks the on-disk artifact that matches the device's supported shader
 * formats and hands it to safi_shader_create_blob(). Callers never have to
 * know which backend is active.
 *
 * File naming contract (see cmake/SafiShaders.cmake):
 *   <shader_dir>/<name>.<stage>.<ext>
 *     stage = "vert" | "frag"
 *     ext   = "spv" | "msl"
 */
#include "safi/render/shader.h"
#include "safi/render/assets.h"
#include "safi/core/log.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *s_stage_ext(SafiShaderStage stage) {
    return (stage == SAFI_SHADER_STAGE_VERTEX) ? "vert" : "frag";
}

/* Priority order: SPIR-V first because it covers Vulkan on every platform
 * (including MoltenVK builds on macOS if the app opts in), then MSL for the
 * native Metal path. DXIL slots in once the build pipeline produces it. */
static SDL_GPUShaderFormat s_pick_format(SDL_GPUShaderFormat supported,
                                         const char **out_file_ext) {
    if (supported & SDL_GPU_SHADERFORMAT_SPIRV) {
        *out_file_ext = "spv";
        return SDL_GPU_SHADERFORMAT_SPIRV;
    }
    if (supported & SDL_GPU_SHADERFORMAT_MSL) {
        *out_file_ext = "msl";
        return SDL_GPU_SHADERFORMAT_MSL;
    }
    *out_file_ext = NULL;
    return SDL_GPU_SHADERFORMAT_INVALID;
}

static void *s_read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    void *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return NULL; }
    *out_size = (size_t)sz;
    return buf;
}

SDL_GPUShader *safi_shader_load(SafiRenderer    *r,
                                const char      *shader_dir,
                                const char      *name,
                                const char      *entrypoint,
                                SafiShaderStage  stage,
                                uint32_t         num_samplers,
                                uint32_t         num_uniform_buffers,
                                uint32_t         num_storage_buffers,
                                uint32_t         num_storage_textures) {
    SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(r->device);
    const char *file_ext = NULL;
    SDL_GPUShaderFormat format = s_pick_format(supported, &file_ext);
    if (format == SDL_GPU_SHADERFORMAT_INVALID) {
        SAFI_LOG_ERROR("safi_shader_load('%s'): no supported shader format "
                       "(device reports 0x%x)", name, (unsigned)supported);
        return NULL;
    }

    /* NULL shader_dir → use the registered shader root (set via
     * SafiAppDesc.shader_root). Keeps shader paths portable across builds
     * and unblocks the M8 shipping story where compile-time paths fail. */
    if (!shader_dir || !shader_dir[0]) {
        shader_dir = safi_assets_shader_root();
        if (!shader_dir || !shader_dir[0]) {
            SAFI_LOG_ERROR("safi_shader_load('%s'): no shader_dir and no "
                           "shader root registered", name);
            return NULL;
        }
    }

    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%s.%s.%s",
                     shader_dir, name, s_stage_ext(stage), file_ext);
    if (n < 0 || n >= (int)sizeof(path)) {
        SAFI_LOG_ERROR("safi_shader_load: path too long for '%s'", name);
        return NULL;
    }

    size_t size = 0;
    void *bytes = s_read_file(path, &size);
    if (!bytes) {
        SAFI_LOG_ERROR("safi_shader_load: cannot read '%s'", path);
        return NULL;
    }

    SDL_GPUShader *sh = safi_shader_create_blob(
        r, bytes, size, format, entrypoint, stage,
        num_samplers, num_uniform_buffers,
        num_storage_buffers, num_storage_textures);

    free(bytes);
    return sh;
}
