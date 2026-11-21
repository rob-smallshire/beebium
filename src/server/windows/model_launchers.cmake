# The Windows per-model launchers (#298): beebium-<model>.exe for each model,
# each a build of model_launcher.c that runs the beebium-server.exe beside it
# with its own command line unchanged. They stand in for the beebium-<model>
# symlinks Linux and macOS install next to the multi-call server.
#
# beebium_add_model_launchers(SERVER_TARGET <target> MODELS <model>...)
#
# Call it from the directory that defines the server target, so the launchers
# build into the same output directory (and, on a multi-config generator, the
# same per-configuration subdirectory) as beebium-server.exe, which is where
# the in-tree tests run the names from. Each launcher is installed to bin/
# beside the server and builds after it. The function sets
# BEEBIUM_MODEL_LAUNCHER_TARGETS in the caller's scope; add those to
# beebium-servers so building the servers builds the launchers.

set(BEEBIUM_MODEL_LAUNCHER_SOURCE_FILEPATH "${CMAKE_CURRENT_LIST_DIR}/model_launcher.c")

function(beebium_add_model_launchers)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "SERVER_TARGET" "MODELS")
    if(NOT arg_SERVER_TARGET OR NOT arg_MODELS)
        message(FATAL_ERROR "beebium_add_model_launchers needs SERVER_TARGET and MODELS")
    endif()
    set(launcher_targets "")
    foreach(model IN LISTS arg_MODELS)
        set(launcher_target "beebium-${model}")
        add_executable(${launcher_target} "${BEEBIUM_MODEL_LAUNCHER_SOURCE_FILEPATH}")
        target_compile_definitions(${launcher_target} PRIVATE
            BEEBIUM_LAUNCHER_MODEL=${model} UNICODE _UNICODE)
        if(MSVC)
            # wmain needs no /SUBSYSTEM change: a console program by default.
            target_compile_options(${launcher_target} PRIVATE /W4 /WX)
        else()
            # MinGW needs -municode to take wmain as the entry point.
            target_compile_options(${launcher_target} PRIVATE -Wall -Wextra -Werror)
            target_link_options(${launcher_target} PRIVATE -municode)
        endif()
        add_dependencies(${launcher_target} ${arg_SERVER_TARGET})
        install(TARGETS ${launcher_target} RUNTIME DESTINATION bin)
        list(APPEND launcher_targets ${launcher_target})
    endforeach()
    set(BEEBIUM_MODEL_LAUNCHER_TARGETS ${launcher_targets} PARENT_SCOPE)
endfunction()
