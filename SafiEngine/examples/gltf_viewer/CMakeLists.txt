add_executable(gltf_viewer
    src/main.c
    src/demo_state.c
    src/scene.c
    src/systems/control_system.c
    src/systems/render_system.c
)

target_include_directories(gltf_viewer PRIVATE include)

target_link_libraries(gltf_viewer
    PRIVATE
        safi_engine
        safi_warnings
)

set_target_properties(gltf_viewer PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# ---- Compile the unlit shader (HLSL -> SPIR-V + MSL) ----------------------
set(_gltf_shader_out "${CMAKE_BINARY_DIR}/shaders")
safi_compile_shader(
    NAME       unlit
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/unlit.hlsl"
    OUTPUT_DIR "${_gltf_shader_out}"
    STAGES     vert frag
    TARGET_VAR _unlit_shader_target
)
add_dependencies(gltf_viewer ${_unlit_shader_target})

# Point the demo at its assets. Model data lives next to this CMakeLists;
# compiled shader artifacts live in the build tree.
target_compile_definitions(gltf_viewer PRIVATE
    SAFI_DEMO_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    SAFI_DEMO_SHADER_DIR="${_gltf_shader_out}"
)
