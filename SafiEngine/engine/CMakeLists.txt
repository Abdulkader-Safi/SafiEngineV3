# engine/CMakeLists.txt
# The SafiEngine static library. Written in pure C11.

file(GLOB_RECURSE SAFI_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
file(GLOB_RECURSE SAFI_ENGINE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

add_library(safi_engine STATIC
    ${SAFI_ENGINE_SOURCES}
    ${SAFI_ENGINE_HEADERS}
)

target_include_directories(safi_engine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(safi_engine
    PUBLIC
        SDL3::SDL3-static
        cglm
        flecs::flecs_static
        cgltf
        stb
        microui
        Jolt
    PRIVATE
        safi_warnings
)

set_target_properties(safi_engine PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# ---- Engine-owned shaders (HLSL -> SPIR-V + MSL) --------------------------
# The MicroUI debug UI pipeline uses a shader that ships with the engine
# itself. Compile it via the shared safi_compile_shader() helper, drop the
# outputs into ${CMAKE_BINARY_DIR}/shaders alongside example shaders, and
# expose that directory to debug_ui.c via a compile definition.
set(_engine_shader_out "${CMAKE_BINARY_DIR}/shaders")
safi_compile_shader(
    NAME       microui
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/shaders/microui.hlsl"
    OUTPUT_DIR "${_engine_shader_out}"
    STAGES     vert frag
    ENTRY_VERT mu_vs
    ENTRY_FRAG mu_fs
    TARGET_VAR _microui_shader_target
)
add_dependencies(safi_engine ${_microui_shader_target})

safi_compile_shader(
    NAME       lit
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/lit.hlsl"
    OUTPUT_DIR "${_engine_shader_out}"
    STAGES     vert frag
    ENTRY_VERT vs_main
    ENTRY_FRAG fs_main
    TARGET_VAR _lit_shader_target
)
add_dependencies(safi_engine ${_lit_shader_target})
target_compile_definitions(safi_engine PRIVATE
    SAFI_ENGINE_SHADER_DIR="${_engine_shader_out}"
    SAFI_ENGINE_FONT_PATH="${CMAKE_SOURCE_DIR}/engine/assets/fonts/ProggyClean.ttf"
)
