// safi_cimgui bridge — exposes the Dear ImGui SDL3 + SDL_gpu backends as
// C-linkage functions so the rest of SafiEngine can stay in pure C.

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

extern "C" {

bool safi_cimgui_init(void *sdl_window, void *gpu_device, unsigned int swapchain_format) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    SDL_Window    *win = static_cast<SDL_Window *>(sdl_window);
    SDL_GPUDevice *dev = static_cast<SDL_GPUDevice *>(gpu_device);

    if (!ImGui_ImplSDL3_InitForSDLGPU(win)) return false;

    ImGui_ImplSDLGPU3_InitInfo init_info{};
    init_info.Device            = dev;
    init_info.ColorTargetFormat = static_cast<SDL_GPUTextureFormat>(swapchain_format);
    init_info.MSAASamples       = SDL_GPU_SAMPLECOUNT_1;
    return ImGui_ImplSDLGPU3_Init(&init_info);
}

void safi_cimgui_shutdown(void) {
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void safi_cimgui_process_event(const void *sdl_event) {
    ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event *>(sdl_event));
}

void safi_cimgui_new_frame(void) {
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void safi_cimgui_render(void *cmd_buffer, void *render_pass) {
    ImGui::Render();
    ImDrawData *draw_data = ImGui::GetDrawData();
    if (!draw_data) return;

    auto *cmd  = static_cast<SDL_GPUCommandBuffer *>(cmd_buffer);
    auto *pass = static_cast<SDL_GPURenderPass *>(render_pass);

    Imgui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd);
    ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd, pass);
}

} // extern "C"
