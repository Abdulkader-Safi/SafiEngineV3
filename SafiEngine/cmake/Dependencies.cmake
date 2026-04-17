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
# Cross-platform shader toolchain: glslang + SPIRV-Cross (option (b) from the
# previous milestone note — we skip SDL_shadercross because its vendored DXC
# fails to build on modern Apple libc++). Shaders are authored once in HLSL,
# compiled to SPIR-V by glslangValidator, then transpiled to MSL by
# spirv-cross. DXIL (for the D3D12 fast-path on Windows) can be layered in
# later via DXC on Windows CI hosts — SPIR-V already covers Vulkan on every
# platform, so correctness is not blocked on DXIL.
# ---------------------------------------------------------------------------

# --- glslang (HLSL/GLSL → SPIR-V) -----------------------------------------
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)            # skip SPIRV-Tools optimizer dep
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE) # we need the glslangValidator CLI
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG        15.0.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glslang)

# --- SPIRV-Cross (SPIR-V → MSL) -------------------------------------------
set(SPIRV_CROSS_CLI ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_GLSL ON CACHE BOOL "" FORCE)   # required by MSL + CLI
set(SPIRV_CROSS_ENABLE_MSL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL ON CACHE BOOL "" FORCE)   # required by the CLI build
set(SPIRV_CROSS_ENABLE_CPP ON CACHE BOOL "" FORCE)    # required by the CLI build
set(SPIRV_CROSS_ENABLE_REFLECT ON CACHE BOOL "" FORCE) # required by the CLI build
set(SPIRV_CROSS_ENABLE_UTIL ON CACHE BOOL "" FORCE)    # required by the CLI build
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(spirv_cross
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
    GIT_TAG        vulkan-sdk-1.3.290.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spirv_cross)
# ---------------------------------------------------------------------------

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

# SDL_gpu uses zero-to-one clip-space depth on every backend (Metal, Vulkan,
# D3D12). Force cglm to match — otherwise glm_perspective emits OpenGL-style
# [-1,1] depth and the near half of the scene gets silently clipped.
target_compile_definitions(cglm INTERFACE CGLM_FORCE_DEPTH_ZERO_TO_ONE)

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
# MicroUI — tiny, pure C immediate-mode UI (~1100 SLOC).
# Produces high-level draw commands (rect, text, icon, clip); the engine
# provides its own SDL_gpu batched-quad backend in engine/src/ui/debug_ui.c.
# ---------------------------------------------------------------------------
FetchContent_Declare(microui
    GIT_REPOSITORY https://github.com/rxi/microui.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(microui)

add_library(microui STATIC ${microui_SOURCE_DIR}/src/microui.c)
target_include_directories(microui PUBLIC ${microui_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
# miniaudio — single-header cross-platform audio (MIT)
# Provides device I/O, mixing graph, 3D spatialization, and built-in wav/flac/mp3
# decoders. The upstream repo ships a CMakeLists that defines a `miniaudio`
# static target compiling the implementation; link it directly.
# ---------------------------------------------------------------------------
set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBVORBIS   ON  CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBOPUS     ON  CACHE BOOL "" FORCE)

FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.22
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(miniaudio)

# Expose the header directory on the existing `miniaudio` static target so
# engine sources can `#include <miniaudio.h>` without further glue.
target_include_directories(miniaudio PUBLIC ${miniaudio_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Jolt Physics — rigid-body simulation (C++ library, wrapped in one .cpp file)
# ---------------------------------------------------------------------------
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS ON CACHE BOOL "" FORCE)
set(CROSS_PLATFORM_DETERMINISTIC OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(OBJECT_LAYER_BITS 16 CACHE STRING "" FORCE)

FetchContent_Declare(JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.2.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  Build
)
FetchContent_MakeAvailable(JoltPhysics)

# ---------------------------------------------------------------------------
# ProggyClean.ttf — public-domain monospaced font for the debug UI.
# ---------------------------------------------------------------------------
set(SAFI_FONT_URL
    "https://raw.githubusercontent.com/ocornut/imgui/master/misc/fonts/ProggyClean.ttf")
set(SAFI_FONT_DST
    "${CMAKE_SOURCE_DIR}/engine/assets/fonts/ProggyClean.ttf")

if(NOT EXISTS "${SAFI_FONT_DST}")
  message(STATUS "SafiEngine: downloading ProggyClean.ttf")
  file(DOWNLOAD
        "${SAFI_FONT_URL}"
        "${SAFI_FONT_DST}"
        SHOW_PROGRESS
        STATUS _font_dl_status
    )
  list(GET _font_dl_status 0 _font_dl_code)
  if(NOT _font_dl_code EQUAL 0)
    message(WARNING "Failed to download ProggyClean.ttf (status=${_font_dl_status}). "
                        "Place a .ttf manually at ${SAFI_FONT_DST}.")
  endif()
endif()

# ---------------------------------------------------------------------------
# cJSON — lightweight JSON parser/writer for scene serialization
# ---------------------------------------------------------------------------
set(ENABLE_CJSON_TEST    OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_AND_STATIC_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

FetchContent_Declare(cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG        v1.7.18
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(cjson)
target_include_directories(cjson PUBLIC $<BUILD_INTERFACE:${cjson_SOURCE_DIR}>)
unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)

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
