/**
 * safi/render/renderer.h — SDL_gpu device wrapper.
 *
 * Owns the SDL_GPUDevice, window, depth texture, and the per-frame command
 * buffer / render pass. One renderer per SafiApp.
 */
#ifndef SAFI_RENDER_RENDERER_H
#define SAFI_RENDER_RENDERER_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

typedef struct SafiRenderer {
    SDL_Window    *window;
    SDL_GPUDevice *device;

    SDL_GPUTexture       *depth_texture;
    uint32_t              depth_w;
    uint32_t              depth_h;
    SDL_GPUTextureFormat  swapchain_format;
    SDL_GPUTextureFormat  depth_format;

    /* Per-frame, valid only between begin_frame and end_frame. */
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTexture       *swapchain_tex;
    uint32_t              swapchain_w;
    uint32_t              swapchain_h;
    SDL_GPURenderPass    *pass;
    bool                  frame_active;
    float                 dpi_scale;
} SafiRenderer;

typedef struct SafiRendererDesc {
    const char *title;
    int         width;
    int         height;
    bool        vsync;
} SafiRendererDesc;

bool safi_renderer_init(SafiRenderer *r, const SafiRendererDesc *desc);
void safi_renderer_shutdown(SafiRenderer *r);

/* Acquire the command buffer + swapchain texture for this frame. Does NOT
 * open a render pass — any copy passes (e.g. ImGui vertex upload) must run
 * between this call and safi_renderer_begin_main_pass. Returns false if the
 * swapchain wasn't ready (e.g. window minimized); skip the frame. */
bool safi_renderer_begin_frame(SafiRenderer *r);

/* Open the engine's main color + depth render pass. Call once per frame
 * after begin_frame and after any pre-pass uploads. */
void safi_renderer_begin_main_pass(SafiRenderer *r);

/* Close the main render pass. */
void safi_renderer_end_main_pass(SafiRenderer *r);

/* Submit the command buffer. */
void safi_renderer_end_frame(SafiRenderer *r);

const char *safi_renderer_backend_name(const SafiRenderer *r);

#endif /* SAFI_RENDER_RENDERER_H */
