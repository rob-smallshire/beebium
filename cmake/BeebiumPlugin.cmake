# BeebiumPlugin.cmake - Shared post-build deployment for extension plugins.
#
# Usage:
#   beebium_finalize_plugin(TARGET beebium_ext_piconet_plugin NAME piconet)
#
# What it does:
#   - Copies the plugin library and its manifest.json into
#     "<server-exe-dir>/extensions/<name>/" so the server's default
#     extension-dir search (exe_parent / "extensions") finds it on every
#     platform and every build configuration.
#   - On Windows additionally copies the plugin DLL into
#     "<build>/tests/<CONFIG>/" so test binaries that link the plugin
#     (or LoadLibrary it) can resolve it through the Windows DLL search,
#     matching the beebium_extension_api.dll / beebium_extension_ui_proto.dll
#     arrangement.
#
# Why a shared helper:
#   Every SHARED plugin (acorn-rtc, acorn-scsi, piconet, scsi-hard-disc,
#   test-scratch-ram) needs identical deployment -- the previous inline
#   manifest-copy pattern dropped manifests next to the build output,
#   which on MSBuild multi-config lands a $(Configuration) directory
#   below where the server's default extension-dir search looks. Placing
#   plugins alongside the server executable sidesteps the multi-config
#   layout mismatch entirely.
#
# TARGET   - the plugin's shared-library target name (e.g. beebium_ext_piconet_plugin)
# NAME     - the short directory name (e.g. "piconet"); also the expected
#            output name of the plugin DLL / .so / .dylib.

function(beebium_finalize_plugin)
    cmake_parse_arguments(ARG "" "TARGET;NAME" "" ${ARGN})

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "beebium_finalize_plugin: TARGET is required")
    endif()
    if(NOT ARG_NAME)
        message(FATAL_ERROR "beebium_finalize_plugin: NAME is required")
    endif()

    # Plugins resolve the extension ABI (and the framework types they use) from
    # the linked beebium_extension_api dylib, and protobuf from the shared
    # libprotobuf, so they link cleanly with no undefined symbols. Extensions no
    # longer host gRPC services -- their APIs go through the core's ExtensionRpc
    # channel -- so the macOS -undefined dynamic_lookup / host-resolved-runtime
    # workaround for gRPC #39198 (and the matching server -rdynamic) is gone.

    # Server-adjacent deploy. Only meaningful when the server executables
    # are being built this configure; skip gracefully otherwise so that
    # tests-only / plugin-only configures still succeed.
    if(BEEBIUM_BUILD_SERVER)
        # $<TARGET_FILE_DIR:beebium-model-b> is evaluated at build-system
        # generation time; no dependency on subdirectory processing order.
        # All three server variants (model-b, model-b-plus, model-b-romram)
        # share the same output directory, so any of them is a valid anchor.
        set(_deploy_dir "$<TARGET_FILE_DIR:beebium-model-b>/extensions/${ARG_NAME}")
        add_custom_command(TARGET ${ARG_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_deploy_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${ARG_TARGET}>
                "${_deploy_dir}/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
                "${_deploy_dir}/manifest.json"
            COMMENT "Deploying ${ARG_NAME} plugin to <server>/extensions/${ARG_NAME}/"
        )
        # Firmware the plugin ships (declared in its manifest's `roms`) lives in
        # the plugin's roms/ directory and deploys beside the library and
        # manifest, so the extension resolves it against its own manifest dir.
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/roms")
            add_custom_command(TARGET ${ARG_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/roms"
                    "${_deploy_dir}/roms"
                COMMENT "Deploying ${ARG_NAME} ROMs to <server>/extensions/${ARG_NAME}/roms/"
            )
        endif()
    endif()

    # Install the plugin alongside the server binaries, matching the runtime
    # discovery layout: <prefix>/bin/extensions/<name>/{<lib>.so, manifest.json}.
    # The server's default extension-dir search is exe_dir/extensions, and the
    # installed server lives in <prefix>/bin, so plugins install under
    # bin/extensions/<name>. The RPATH lets a freshly dlopen'd plugin resolve
    # the extension ABI libraries in <prefix>/lib even when not already loaded
    # by the host process (../../../lib: name -> extensions -> bin -> prefix).
    if(BEEBIUM_BUILD_SERVER)
        if(APPLE)
            set_target_properties(${ARG_TARGET} PROPERTIES
                INSTALL_RPATH "@loader_path/../../../lib")
        elseif(NOT WIN32)
            set_target_properties(${ARG_TARGET} PROPERTIES
                INSTALL_RPATH "$ORIGIN/../../../lib")
        endif()
        install(TARGETS ${ARG_TARGET}
            LIBRARY DESTINATION bin/extensions/${ARG_NAME}
        )
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
            DESTINATION bin/extensions/${ARG_NAME}
        )
        # Ship the plugin's firmware beside it, so every package and the macOS
        # app bundle (which copies extensions/ whole) carries it.
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/roms")
            install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/roms
                DESTINATION bin/extensions/${ARG_NAME}
            )
        endif()
    endif()

    # Windows test runtime: any test binary that statically links the
    # plugin (or LoadLibrary's it directly) needs the DLL findable via
    # the Windows DLL search (which looks in the loading executable's
    # directory). Mirrors the existing copy steps for
    # beebium_extension_api.dll / beebium_extension_ui_proto.dll.
    if(WIN32 AND BEEBIUM_BUILD_TESTS)
        add_custom_command(TARGET ${ARG_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_BINARY_DIR}/tests/$<CONFIG>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${ARG_TARGET}>
                "${CMAKE_BINARY_DIR}/tests/$<CONFIG>/"
            COMMENT "Copying ${ARG_NAME}.dll to tests/$<CONFIG>/ for runtime loading"
        )
    endif()
endfunction()
