/*
 * render_system.h — Per-frame render orchestration for the gltf_viewer demo.
 *
 * Runs on EcsOnStore. The SafiApp pointer is passed via the ecs_system ctx
 * slot so the callback can reach the renderer without a global.
 *
 * Frame shape (SDL_gpu forbids nested passes, so the UI's vertex upload
 * must happen BEFORE the main render pass opens):
 *
 *   begin_frame                    -> cmd + swapchain
 *   debug_ui_begin_frame           -> mu_begin, ready for widget calls
 *   <build widgets>
 *   debug_ui_prepare               -> mu_end + batch quads + copy-pass upload
 *   begin_main_pass                -> open color+depth pass
 *   <draw mesh>
 *   debug_ui_render                -> record batched draws into the pass
 *   end_main_pass
 *   end_frame                      -> submit
 */
#ifndef GLTF_VIEWER_RENDER_SYSTEM_H
#define GLTF_VIEWER_RENDER_SYSTEM_H

#include <safi/safi.h>

void render_system(ecs_iter_t *it);

#endif /* GLTF_VIEWER_RENDER_SYSTEM_H */
