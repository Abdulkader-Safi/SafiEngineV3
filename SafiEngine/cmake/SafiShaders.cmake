# cmake/SafiShaders.cmake
#
# Offline shader build for SafiEngine. Compiles an HLSL source file into
# per-stage SPIR-V blobs via glslangValidator, then transpiles each blob to
# MSL via spirv-cross. The runtime loader (safi_shader_load) picks the
# right artifact based on SDL_GetGPUShaderFormats() at startup.
#
#   safi_compile_shader(
#       NAME          <stem>                 # e.g. unlit
#       SOURCE        <absolute path.hlsl>
#       OUTPUT_DIR    <absolute dir>         # where .spv/.msl land
#       ENTRY_VERT    <name>                 # default: vs_main
#       ENTRY_FRAG    <name>                 # default: fs_main
#       STAGES        vert frag              # subset of {vert, frag}
#       TARGET_VAR    <out var>              # receives custom-target name
#   )
#
# The generated custom target is named safi_shaders_<NAME>. Add it to a
# consumer target with add_dependencies().
# ---------------------------------------------------------------------------

function(safi_compile_shader)
    set(_opts)
    set(_one NAME SOURCE OUTPUT_DIR ENTRY_VERT ENTRY_FRAG TARGET_VAR)
    set(_multi STAGES)
    cmake_parse_arguments(SSH "${_opts}" "${_one}" "${_multi}" ${ARGN})

    if(NOT SSH_NAME OR NOT SSH_SOURCE OR NOT SSH_OUTPUT_DIR)
        message(FATAL_ERROR "safi_compile_shader: NAME, SOURCE and OUTPUT_DIR are required")
    endif()
    if(NOT SSH_STAGES)
        set(SSH_STAGES vert frag)
    endif()
    if(NOT SSH_ENTRY_VERT)
        set(SSH_ENTRY_VERT vs_main)
    endif()
    if(NOT SSH_ENTRY_FRAG)
        set(SSH_ENTRY_FRAG fs_main)
    endif()

    file(MAKE_DIRECTORY "${SSH_OUTPUT_DIR}")

    set(_outputs "")
    foreach(_stage IN LISTS SSH_STAGES)
        if(_stage STREQUAL "vert")
            set(_entry "${SSH_ENTRY_VERT}")
        elseif(_stage STREQUAL "frag")
            set(_entry "${SSH_ENTRY_FRAG}")
        else()
            message(FATAL_ERROR "safi_compile_shader: unsupported stage '${_stage}'")
        endif()

        set(_spv "${SSH_OUTPUT_DIR}/${SSH_NAME}.${_stage}.spv")
        set(_msl "${SSH_OUTPUT_DIR}/${SSH_NAME}.${_stage}.msl")

        # HLSL -> SPIR-V. glslangValidator picks HLSL mode from the .hlsl
        # extension; we pass -D to make it explicit and -e to select the
        # stage entry point. SDL_GPU expects standard Vulkan resource
        # bindings in HLSL source (register(bN, spaceM) etc.) — see
        # assets/shaders/unlit.hlsl for the binding contract.
        #
        # Note: we reference the build executables via $<TARGET_FILE:...>
        # and list the unaliased target names in DEPENDS. Using the alias
        # (glslang::glslang-standalone) directly as a Make dependency
        # produces ":" characters the Make generator cannot parse.
        add_custom_command(
            OUTPUT  "${_spv}"
            COMMAND $<TARGET_FILE:glslang-standalone>
                    -V -D -S ${_stage} -e ${_entry}
                    -o "${_spv}" "${SSH_SOURCE}"
            DEPENDS "${SSH_SOURCE}" glslang-standalone
            COMMENT "HLSL -> SPIR-V: ${SSH_NAME}.${_stage}"
            VERBATIM
        )

        # SPIR-V -> MSL. Entry-point name is preserved from the SPIR-V
        # (spirv-cross only rewrites names that collide with reserved
        # words, and vs_main/fs_main are fine). SDL_CreateGPUShader is
        # called with the same entry-point string on every backend.
        add_custom_command(
            OUTPUT  "${_msl}"
            COMMAND $<TARGET_FILE:spirv-cross>
                    --msl --msl-version 20000
                    --output "${_msl}" "${_spv}"
            DEPENDS "${_spv}" spirv-cross
            COMMENT "SPIR-V -> MSL: ${SSH_NAME}.${_stage}"
            VERBATIM
        )

        list(APPEND _outputs "${_spv}" "${_msl}")
    endforeach()

    set(_tgt "safi_shaders_${SSH_NAME}")
    add_custom_target(${_tgt} ALL DEPENDS ${_outputs})

    if(SSH_TARGET_VAR)
        set(${SSH_TARGET_VAR} "${_tgt}" PARENT_SCOPE)
    endif()
endfunction()
