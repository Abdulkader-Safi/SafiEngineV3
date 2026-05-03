/**
 * safi/render/preview.h — one-shot model rendering into an offscreen texture.
 *
 * Wraps safi_renderer_begin_offscreen_pass + safi_model_draw_lit with a
 * minimal camera fit (orthographic, framed to the model's AABB) and a
 * single key-light + ambient lighting setup. Aimed at asset-browser
 * thumbnails and a future mesh-preview pane in the editor.
 *
 * Caller responsibilities:
 *  - Renderer must be inside an active frame (between begin_frame and
 *    end_frame). No other render pass may be open at call time.
 *  - `target` must be created with SAMPLER + COLOR_TARGET usage.
 *  - `depth` must match width/height and have DEPTH_STENCIL_TARGET usage.
 *    Pass NULL to skip the depth attachment (single-primitive thumbnails
 *    that don't self-occlude can omit it).
 */
#ifndef SAFI_RENDER_PREVIEW_H
#define SAFI_RENDER_PREVIEW_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL_gpu.h>

#include "safi/render/renderer.h"
#include "safi/render/model.h"

bool safi_preview_render_model(SafiRenderer   *r,
                               SafiModel      *model,
                               SDL_GPUTexture *target,
                               SDL_GPUTexture *depth,
                               uint32_t        width,
                               uint32_t        height);

#endif /* SAFI_RENDER_PREVIEW_H */
