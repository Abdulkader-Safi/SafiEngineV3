# cmake/Dependencies.cmake
# All third-party libraries are fetched here so users only need CMake + a
# C/C++ compiler. No manual `apt install` / `brew install` steps.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------------------
# SDL3 — windowing + input + SDL_gpu (Vulkan / Metal / D3D12)
# ---------------------------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.14
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SDL3)

# ---------------------------------------------------------------------------
# SDL_shadercross — HLSL → SPIR-V/MSL/DXIL at runtime (no offline toolchain)
# ---------------------------------------------------------------------------
set(SDLSHADERCROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_STATIC ON  CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_CLI    OFF CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_VENDORED ON CACHE BOOL "" FORCE)
set(SDLSHADERCROSS_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL_shadercross
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_shadercross.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SDL_shadercross)

# ---------------------------------------------------------------------------
# cglm — C SIMD math (vec/mat/quat)
# ---------------------------------------------------------------------------
set(CGLM_STATIC ON  CACHE BOOL "" FORCE)
set(CGLM_SHARED OFF CACHE BOOL "" FORCE)
set(CGLM_USE_TEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(cglm
    GIT_REPOSITORY https://github.com/recp/cglm.git
    GIT_TAG        v0.9.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(cglm)

# ---------------------------------------------------------------------------
# flecs — Bevy-like ECS in pure C
# ---------------------------------------------------------------------------
set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
set(FLECS_STATIC ON  CACHE BOOL "" FORCE)
set(FLECS_TESTS  OFF CACHE BOOL "" FORCE)
set(FLECS_PIC    ON  CACHE BOOL "" FORCE)

FetchContent_Declare(flecs
    GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
    GIT_TAG        v4.0.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(flecs)

# ---------------------------------------------------------------------------
# cgltf — single-header glTF 2.0 loader
# ---------------------------------------------------------------------------
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.14
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(cgltf)

add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE ${cgltf_SOURCE_DIR})

# ---------------------------------------------------------------------------
# stb — stb_image single-header PNG/JPG decoder
# ---------------------------------------------------------------------------
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

# ---------------------------------------------------------------------------
# cimgui — official C bindings for Dear ImGui (bundles imgui via submodule)
# Built together with the SDL3 + SDL_gpu backends into one static lib.
# ---------------------------------------------------------------------------
FetchContent_Declare(cimgui
    GIT_REPOSITORY https://github.com/cimgui/cimgui.git
    GIT_TAG        docking_inter
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(cimgui)

set(CIMGUI_DIR ${cimgui_SOURCE_DIR})
set(IMGUI_DIR  ${cimgui_SOURCE_DIR}/imgui)

add_library(safi_cimgui STATIC
    ${CIMGUI_DIR}/cimgui.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp
    ${CMAKE_CURRENT_LIST_DIR}/cimgui_bridge.cpp
)
target_include_directories(safi_cimgui
    PUBLIC
        ${CIMGUI_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)
target_compile_definitions(safi_cimgui
    PUBLIC
        IMGUI_DEFINE_MATH_OPERATORS
        IMGUI_DISABLE_OBSOLETE_FUNCTIONS
        # Tell cimgui to export C symbols with default visibility.
        IMGUI_IMPL_API=extern\ \"C\"
)
target_link_libraries(safi_cimgui PUBLIC SDL3::SDL3-static)
set_target_properties(safi_cimgui PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Sample glTF asset — fetched at configure time so the demo runs offline.
# ---------------------------------------------------------------------------
set(SAFI_SAMPLE_GLB_URL
    "https://github.com/KhronosGroup/glTF-Sample-Assets/raw/main/Models/BoxTextured/glTF-Binary/BoxTextured.glb")
set(SAFI_SAMPLE_GLB_DST
    "${CMAKE_SOURCE_DIR}/examples/gltf_viewer/assets/models/BoxTextured.glb")

if(NOT EXISTS "${SAFI_SAMPLE_GLB_DST}")
    message(STATUS "SafiEngine: downloading BoxTextured.glb sample asset")
    file(DOWNLOAD
        "${SAFI_SAMPLE_GLB_URL}"
        "${SAFI_SAMPLE_GLB_DST}"
        SHOW_PROGRESS
        STATUS _dl_status
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        message(WARNING "Failed to download BoxTextured.glb (status=${_dl_status}). "
                        "Place a .glb manually at ${SAFI_SAMPLE_GLB_DST}.")
    endif()
endif()
