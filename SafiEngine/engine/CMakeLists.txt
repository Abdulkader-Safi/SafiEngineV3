# engine/CMakeLists.txt
# The SafiEngine static library. Written in pure C11.

file(GLOB_RECURSE SAFI_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
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
        nuklear
    PRIVATE
        safi_warnings
)

set_target_properties(safi_engine PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
