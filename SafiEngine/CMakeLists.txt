cmake_minimum_required(VERSION 3.24)
project(SafiEngine
    VERSION 0.1.0
    DESCRIPTION "A pure-C game engine built on SDL3 GPU, flecs and Nuklear"
    LANGUAGES C CXX
)

# ---- Global settings -------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "Generate compile_commands.json")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# ---- Dependencies (FetchContent, zero manual install) ---------------------
include(Dependencies)

# ---- Shader build pipeline (HLSL -> SPIR-V + MSL) -------------------------
include(SafiShaders)

# ---- Clangd / Zed IDE support ---------------------------------------------
include(ClangdSetup)

# ---- Warnings --------------------------------------------------------------
add_library(safi_warnings INTERFACE)
if(MSVC)
    target_compile_options(safi_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(safi_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
endif()

# ---- Engine library --------------------------------------------------------
add_subdirectory(engine)

# ---- Examples --------------------------------------------------------------
add_subdirectory(examples/gltf_viewer)
