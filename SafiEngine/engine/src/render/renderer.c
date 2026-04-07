#include "safi/render/renderer.h"
#include "safi/core/log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <string.h>

static bool s_create_depth(SafiRenderer *r, uint32_t w, uint32_t h) {
    if (r->depth_texture) {
        SDL_ReleaseGPUTexture(r->device, r->depth_texture);
        r->depth_texture = NULL;
    }

    SDL_GPUTextureCreateInfo info = {
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        .width                = w,
        .height               = h,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
        .sample_count         = SDL_GPU_SAMPLECOUNT_1,
        .usage                = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
    };
    r->depth_texture = SDL_CreateGPUTexture(r->device, &info);
    if (!r->depth_texture) {
        SAFI_LOG_ERROR("SDL_CreateGPUTexture(depth) failed: %s", SDL_GetError());
        return false;
    }
    r->depth_w = w;
    r->depth_h = h;
    r->depth_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    return true;
}

bool safi_renderer_init(SafiRenderer *r, const SafiRendererDesc *desc) {
    memset(r, 0, sizeof(*r));

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SAFI_LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    r->window = SDL_CreateWindow(desc->title,
                                 desc->width, desc->height,
                                 SDL_WINDOW_RESIZABLE |
                                 SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!r->window) {
        SAFI_LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    /* Let SDL pick the best backend: Metal on macOS, Vulkan on Linux,
     * D3D12 or Vulkan on Windows. */
    r->device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV |
        SDL_GPU_SHADERFORMAT_MSL   |
        SDL_GPU_SHADERFORMAT_DXIL,
        /*debug_mode=*/true,
        /*name=*/NULL);
    if (!r->device) {
        SAFI_LOG_ERROR("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    if (!SDL_ClaimWindowForGPUDevice(r->device, r->window)) {
        SAFI_LOG_ERROR("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    SDL_SetGPUSwapchainParameters(
        r->device, r->window,
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
        desc->vsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_MAILBOX);

    r->swapchain_format =
        SDL_GetGPUSwapchainTextureFormat(r->device, r->window);

    int pw = desc->width, ph = desc->height;
    SDL_GetWindowSizeInPixels(r->window, &pw, &ph);
    r->dpi_scale = (desc->width > 0) ? (float)pw / (float)desc->width : 1.0f;
    if (!s_create_depth(r, (uint32_t)pw, (uint32_t)ph)) return false;

    SAFI_LOG_INFO("SafiRenderer ready — backend: %s",
                  SDL_GetGPUDeviceDriver(r->device));
    return true;
}

void safi_renderer_shutdown(SafiRenderer *r) {
    if (!r) return;
    if (r->device) {
        if (r->depth_texture) SDL_ReleaseGPUTexture(r->device, r->depth_texture);
        if (r->window) SDL_ReleaseWindowFromGPUDevice(r->device, r->window);
        SDL_DestroyGPUDevice(r->device);
    }
    if (r->window) SDL_DestroyWindow(r->window);
    memset(r, 0, sizeof(*r));
    SDL_Quit();
}

bool safi_renderer_begin_frame(SafiRenderer *r) {
    r->cmd = SDL_AcquireGPUCommandBuffer(r->device);
    if (!r->cmd) {
        SAFI_LOG_ERROR("SDL_AcquireGPUCommandBuffer: %s", SDL_GetError());
        return false;
    }

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(r->cmd, r->window,
                                               &r->swapchain_tex,
                                               &r->swapchain_w,
                                               &r->swapchain_h)) {
        SDL_SubmitGPUCommandBuffer(r->cmd);
        r->cmd = NULL;
        return false;
    }
    if (!r->swapchain_tex) {
        SDL_SubmitGPUCommandBuffer(r->cmd);
        r->cmd = NULL;
        return false;
    }

    if (r->swapchain_w != r->depth_w || r->swapchain_h != r->depth_h) {
        s_create_depth(r, r->swapchain_w, r->swapchain_h);
        int lw, lh;
        SDL_GetWindowSize(r->window, &lw, &lh);
        r->dpi_scale = (lw > 0) ? (float)r->swapchain_w / (float)lw : 1.0f;
    }

    r->pass = NULL;
    r->frame_active = true;
    return true;
}

void safi_renderer_begin_main_pass(SafiRenderer *r) {
    if (!r->frame_active || r->pass) return;

    SDL_GPUColorTargetInfo color_target = {
        .texture     = r->swapchain_tex,
        .clear_color = (SDL_FColor){ 0.07f, 0.08f, 0.10f, 1.0f },
        .load_op     = SDL_GPU_LOADOP_CLEAR,
        .store_op    = SDL_GPU_STOREOP_STORE,
    };
    SDL_GPUDepthStencilTargetInfo depth_target = {
        .texture          = r->depth_texture,
        .clear_depth      = 1.0f,
        .load_op          = SDL_GPU_LOADOP_CLEAR,
        .store_op         = SDL_GPU_STOREOP_DONT_CARE,
        .stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE,
        .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
    };
    r->pass = SDL_BeginGPURenderPass(r->cmd, &color_target, 1, &depth_target);
}

void safi_renderer_end_main_pass(SafiRenderer *r) {
    if (!r->pass) return;
    SDL_EndGPURenderPass(r->pass);
    r->pass = NULL;
}

void safi_renderer_end_frame(SafiRenderer *r) {
    if (!r->frame_active) return;
    if (r->pass) {
        SDL_EndGPURenderPass(r->pass);
        r->pass = NULL;
    }
    SDL_SubmitGPUCommandBuffer(r->cmd);
    r->cmd = NULL;
    r->swapchain_tex = NULL;
    r->frame_active = false;
}

const char *safi_renderer_backend_name(const SafiRenderer *r) {
    return r->device ? SDL_GetGPUDeviceDriver(r->device) : "<null>";
}
