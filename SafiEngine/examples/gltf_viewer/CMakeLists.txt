add_executable(gltf_viewer main.c)

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

# Point the demo at its assets. The shader + model live beside this
# CMakeLists.txt; we pass their absolute paths as compile definitions.
target_compile_definitions(gltf_viewer PRIVATE
    SAFI_DEMO_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
