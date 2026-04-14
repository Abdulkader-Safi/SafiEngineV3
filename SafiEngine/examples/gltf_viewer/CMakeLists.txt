add_executable(gltf_viewer
    src/main.c
    src/demo_state.c
    src/scene.c
    src/systems/control_system.c
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

# ---- Copy bundled audio samples from SDL3's test tree -------------------
# SDL3 ships public-domain WAVs in its test/ directory. Copy two into the
# demo's assets so the audio demo works offline without downloads.
set(_audio_dst "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio")
file(MAKE_DIRECTORY "${_audio_dst}")
if(EXISTS "${sdl3_SOURCE_DIR}/test/sample.wav")
    configure_file("${sdl3_SOURCE_DIR}/test/sample.wav" "${_audio_dst}/ambient.wav" COPYONLY)
endif()
if(EXISTS "${sdl3_SOURCE_DIR}/test/sword.wav")
    configure_file("${sdl3_SOURCE_DIR}/test/sword.wav" "${_audio_dst}/click.wav" COPYONLY)
endif()
