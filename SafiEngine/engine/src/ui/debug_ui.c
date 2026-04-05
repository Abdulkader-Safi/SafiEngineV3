/*
 * Tiny C bridge over the Dear ImGui SDL3 + SDL_gpu backends.
 *
 * The heavy lifting lives in ui/debug_ui_bridge.cpp (compiled as C++ and
 * linked into the safi_engine library). These C wrappers keep the public
 * engine API in plain C.
 */
#include "safi/ui/debug_ui.h"

/* Symbols provided by debug_ui_bridge.cpp */
bool safi_cimgui_init(void *sdl_window, void *gpu_device, unsigned int swapchain_format);
void safi_cimgui_shutdown(void);
void safi_cimgui_process_event(const void *sdl_event);
void safi_cimgui_new_frame(void);
void safi_cimgui_render(void *cmd_buffer, void *render_pass);

bool safi_debug_ui_init(SafiRenderer *r) {
    return safi_cimgui_init((void *)r->window,
                            (void *)r->device,
                            (unsigned int)r->swapchain_format);
}

void safi_debug_ui_shutdown(SafiRenderer *r) {
    (void)r;
    safi_cimgui_shutdown();
}

void safi_debug_ui_process_event(const void *sdl_event) {
    safi_cimgui_process_event(sdl_event);
}

void safi_debug_ui_begin_frame(SafiRenderer *r) {
    (void)r;
    safi_cimgui_new_frame();
}

void safi_debug_ui_render(SafiRenderer *r) {
    safi_cimgui_render((void *)r->cmd, (void *)r->pass);
}
