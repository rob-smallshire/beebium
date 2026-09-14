# beebium server executables - one per machine type

# Common include directory for server templates
set(SERVER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Presets output directory. The install() rules below read presets from here,
# so it MUST be a fixed, config-independent path: install(DIRECTORY) does not
# evaluate generator expressions. Presets are generated straight into this
# directory (below), and on multi-config generators are also copied beside the
# per-config executable for the server's exe-relative discovery from the build
# tree.
set(PRESETS_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/presets")
file(MAKE_DIRECTORY ${PRESETS_OUTPUT_DIR})

# Generate a system preset and its thumbnail at build time, using the same
# create-preset code path that users create their own presets with. Extra
# create-preset arguments (e.g. --fdc, --sideways) are forwarded via ARGN.
function(add_system_preset EXECUTABLE PRESET_ID PRESET_NAME RELEASE_DATE)
    # DESCRIPTION is a one-value keyword forwarded as create-preset's
    # --description (the concise one-liner the picker card shows); the rest of
    # ARGN goes to create-preset unchanged.
    cmake_parse_arguments(PRESET "" "DESCRIPTION" "" ${ARGN})
    set(description_args "")
    if(DEFINED PRESET_DESCRIPTION)
        set(description_args --description "${PRESET_DESCRIPTION}")
    endif()
    # Generate into the config-independent PRESETS_OUTPUT_DIR that install()
    # reads. (Writing to $<TARGET_FILE_DIR:...>/presets instead put the presets
    # in a per-config Release/presets on multi-config generators, where the
    # fixed install() source never found them -- so the Windows bundle shipped
    # an empty share/beebium/presets.)
    set(preset_filepath "${PRESETS_OUTPUT_DIR}/${PRESET_ID}.preset.beebium")
    set(thumbnail_filepath "${PRESETS_OUTPUT_DIR}/${PRESET_ID}.thumbnail.png")
    add_custom_command(TARGET ${EXECUTABLE} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PRESETS_OUTPUT_DIR}"
        COMMAND ${EXECUTABLE} create-preset
            --name "${PRESET_NAME}"
            --release-date "${RELEASE_DATE}"
            ${description_args}
            ${PRESET_UNPARSED_ARGUMENTS}
            --output "${preset_filepath}"
        COMMAND ${EXECUTABLE} capture-screenshot
            --preset "${preset_filepath}"
            --output "${thumbnail_filepath}"
            --rom-dir "${BEEBIUM_ROM_DIR}"
            --crop auto
        COMMENT "Generating preset ${PRESET_ID} and its thumbnail"
        VERBATIM
    )
    # Multi-config generators (e.g. Visual Studio) nest the executable in a
    # per-config subdirectory, so PRESETS_OUTPUT_DIR is not beside it. Copy the
    # presets next to the binary too, for exe-relative discovery from the build
    # tree. On single-config generators PRESETS_OUTPUT_DIR already IS
    # $<TARGET_FILE_DIR:...>, so no copy is needed.
    if(CMAKE_CONFIGURATION_TYPES)
        add_custom_command(TARGET ${EXECUTABLE} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${EXECUTABLE}>/presets"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${preset_filepath}"
                "${thumbnail_filepath}"
                "$<TARGET_FILE_DIR:${EXECUTABLE}>/presets/"
            VERBATIM
        )
    endif()
endfunction()

# A system preset that references a dlopened PLUGIN extension (a Tube
# coprocessor). Unlike add_system_preset, generation cannot hang off the
# server's POST_BUILD: the plugin deploys into <server>/extensions via its OWN
# POST_BUILD and its target depends on the server (it deploys into the server's
# directory), so a fresh build would run create-preset BEFORE the plugin exists
# ("Unknown argument: --tube-65c02"). Generate via a custom target that DEPENDS
# on the plugin -- which transitively builds the server first and deploys the
# plugin -- so the extension resolves. THUMBNAIL_DELAY is forwarded to
# capture-screenshot as --duration (the parasite's "Acorn TUBE ..." banner
# settles after the 2.0s default); the rest of ARGN goes to create-preset.
function(add_plugin_preset EXECUTABLE PLUGIN_TARGET PRESET_ID PRESET_NAME RELEASE_DATE)
    cmake_parse_arguments(PRESET "" "THUMBNAIL_DELAY;DESCRIPTION" "" ${ARGN})
    set(capture_duration_args "")
    if(DEFINED PRESET_THUMBNAIL_DELAY)
        set(capture_duration_args --duration ${PRESET_THUMBNAIL_DELAY})
    endif()
    set(description_args "")
    if(DEFINED PRESET_DESCRIPTION)
        set(description_args --description "${PRESET_DESCRIPTION}")
    endif()
    set(preset_filepath "${PRESETS_OUTPUT_DIR}/${PRESET_ID}.preset.beebium")
    set(thumbnail_filepath "${PRESETS_OUTPUT_DIR}/${PRESET_ID}.thumbnail.png")
    set(copy_commands "")
    if(CMAKE_CONFIGURATION_TYPES)
        set(copy_commands
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${EXECUTABLE}>/presets"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${preset_filepath}" "${thumbnail_filepath}"
                "$<TARGET_FILE_DIR:${EXECUTABLE}>/presets/")
    endif()
    add_custom_command(
        OUTPUT "${preset_filepath}" "${thumbnail_filepath}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PRESETS_OUTPUT_DIR}"
        COMMAND ${EXECUTABLE} create-preset
            --name "${PRESET_NAME}"
            --release-date "${RELEASE_DATE}"
            ${description_args}
            ${PRESET_UNPARSED_ARGUMENTS}
            --output "${preset_filepath}"
        COMMAND ${EXECUTABLE} capture-screenshot
            --preset "${preset_filepath}"
            --output "${thumbnail_filepath}"
            --rom-dir "${BEEBIUM_ROM_DIR}"
            --crop auto
            ${capture_duration_args}
        ${copy_commands}
        DEPENDS ${EXECUTABLE} ${PLUGIN_TARGET}
        COMMENT "Generating plugin preset ${PRESET_ID} and its thumbnail"
        VERBATIM
    )
    add_custom_target(preset_${PRESET_ID} ALL
        DEPENDS "${preset_filepath}" "${thumbnail_filepath}")
    # Unlike add_system_preset (whose POST_BUILD runs when beebium-servers builds
    # the executable), this preset is its own target, so wire it into the
    # beebium-servers aggregate that CI and the release build actually build --
    # otherwise `cmake --build --target beebium-servers` never generates it.
    if(TARGET beebium-servers)
        add_dependencies(beebium-servers preset_${PRESET_ID})
    endif()
endfunction()

# Function to create a beebium server executable for a specific machine type
function(add_beebium_server TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})

    # The server main is the single most memory-hungry TU in the tree (full
    # Machine<Hardware> template + static gRPC/protobuf headers). Pin its
    # compilation to the depth-1 beebium_heavy_compile pool (defined in the
    # top-level CMakeLists) so a parallel Ninja build never compiles two of
    # them at once and OOM-kills the compiler on memory-limited hosts.
    set_target_properties(${TARGET_NAME} PROPERTIES
        JOB_POOL_COMPILE beebium_heavy_compile)

    target_include_directories(${TARGET_NAME}
        PRIVATE
            ${SERVER_INCLUDE_DIR}
    )

    target_compile_definitions(${TARGET_NAME}
        PRIVATE
            BEEBIUM_VERSION="${PROJECT_VERSION}"
    )

    # Most Beebium extensions ship as plugins, deployed via
    # beebium_finalize_plugin() to <exe-dir>/extensions/<name>/, and
    # loaded at runtime through PluginLoader. Plugin targets are
    # aggregated into beebium-servers (the convenience target below)
    # so a single build invocation produces a complete extension tree.
    #
    # The built-in exceptions (aun and host-serial) are statically linked.
    # See BuiltinExtensions.hpp for their rationale and what would be required
    # to convert each to a plugin. acorn-65c02-coprocessor is now a plugin.
    target_link_libraries(${TARGET_NAME}
        PRIVATE
            beebium_service
            beebium_ext_aun
            beebium_ext_host_serial
            nlohmann_json::nlohmann_json
            stb_image_write
    )

    # No -rdynamic needed: dlopened plugins resolve the extension ABI from the
    # linked beebium_extension_api dylib, not from the host executable's exported
    # symbols. Extensions no longer host gRPC services (their APIs go through the
    # core's ExtensionRpc channel), so the host no longer needs to export a
    # shared gRPC/protobuf runtime for plugins to bind to.

    # Installed layout: server binaries live in <prefix>/bin and resolve the
    # extension ABI shared libraries from <prefix>/lib via an $ORIGIN-relative
    # RPATH (@loader_path on macOS). This only affects the installed binaries;
    # the build-tree RPATH (auto-added by CMake to build/lib) is unaffected, so
    # running from the build directory continues to work unchanged.
    if(APPLE)
        set_target_properties(${TARGET_NAME} PROPERTIES
            INSTALL_RPATH "@loader_path/../lib")
    elseif(NOT WIN32)
        set_target_properties(${TARGET_NAME} PROPERTIES
            INSTALL_RPATH "$ORIGIN/../lib")
    endif()
endfunction()

# Model-specific executables
add_beebium_server(beebium-model-b main_model_b.cpp)
add_beebium_server(beebium-model-b-plus main_model_b_plus.cpp)
add_beebium_server(beebium-model-b-plus-128k main_model_b_plus_128k.cpp)
add_beebium_server(beebium-model-b-romram main_model_b_romram.cpp)

# System presets are generated below (after the ROM copy) via add_system_preset,
# which runs the create-preset subcommand as a POST_BUILD step.

# Convenience target to build all server executables and their bundled
# extension plugins. Plugins are loaded at runtime from
# <exe-dir>/extensions/<name>/ via beebium_finalize_plugin() post-build
# deployment, not linked, so we list them here to ensure a complete
# build in one invocation.
set(BEEBIUM_SERVERS_DEPS
    beebium-model-b
    beebium-model-b-plus
    beebium-model-b-plus-128k
    beebium-model-b-romram
)
if(BEEBIUM_BUILD_SERVICE)
    list(APPEND BEEBIUM_SERVERS_DEPS
        beebium_ext_acorn_65c02_plugin
        beebium_ext_acorn_65c102_plugin
        beebium_ext_acorn_rtc_plugin
        beebium_ext_acorn_scsi_plugin
        beebium_ext_piconet_plugin
        beebium_ext_scsi_hard_disc_plugin
        beebium_ext_test_scratch_ram_plugin
        beebium_ext_loopback_serial_plugin
        beebium_ext_ip232_serial_plugin
        beebium_ext_rpc_serial_plugin
        beebium_ext_rfc2217_client_plugin
        beebium_ext_rfc2217_server_plugin
    )
endif()
add_custom_target(beebium-servers DEPENDS ${BEEBIUM_SERVERS_DEPS})

# Copy ROMs to build output directory for development
set(ROM_OUTPUT_DIR "${CMAKE_BINARY_DIR}/roms")
file(MAKE_DIRECTORY ${ROM_OUTPUT_DIR})

add_custom_command(TARGET beebium-model-b POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BEEBIUM_ROM_DIR}/acorn-mos_1_20.rom"
        "${BEEBIUM_ROM_DIR}/acorn-mos_2_0.rom"
        "${BEEBIUM_ROM_DIR}/bbc-basic_2.rom"
        "${BEEBIUM_ROM_DIR}/acorn-dfs_2_26.rom"
        # ANFS: the shipped Econet presets reference it, so a developer running
        # start --preset from the build tree needs it beside the binary too.
        "${BEEBIUM_ROM_DIR}/acorn-anfs_4_18.rom"
        "${ROM_OUTPUT_DIR}/"
    COMMENT "Copying host ROMs to build directory"
)

# Generate the shipped system presets (and their thumbnails). Model B serves
# two presets: a bare cassette machine and a disc-equipped variant. A filing
# system is provisioned explicitly via --sideways; no machine auto-loads DFS
# except the Model B+, whose disc controller and DFS are integral hardware.
add_system_preset(beebium-model-b "model-b" "Model B" "1981-12"
    DESCRIPTION "32K Model B, no disc")
add_system_preset(beebium-model-b "model-b-disc" "Model B (Disc)" "1982"
    DESCRIPTION "32K Model B, 1770 DFS"
    --fdc acorn-1770 --sideways 14:rom:acorn-dfs_2_26.rom)
add_system_preset(beebium-model-b-romram "model-b-romram-disc"
    "Model B with ROM/RAM Board (Disc)" "1984"
    DESCRIPTION "Model B, ROM/RAM board, 1770 DFS"
    --fdc acorn-1770 --sideways 14:rom:acorn-dfs_2_26.rom)
add_system_preset(beebium-model-b-plus "model-b-plus" "Model B+ 64K" "1985-06"
    DESCRIPTION "64K Model B+, integral DFS")
add_system_preset(beebium-model-b-plus-128k "model-b-plus-128k"
    "Model B+ 128K" "1985-06"
    DESCRIPTION "128K Model B+, sideways RAM")

# Econet presets, so users can try networking out of the box: model-b-disc
# plus a network filing system (ANFS 4.18) and a station number, one per
# transport. ANFS sits in slot 14 and DFS in slot 13 (not slot 14 as the plain
# disc preset uses): the MOS polls filing-system ROMs from the top socket down,
# so the higher-slotted ANFS becomes the default filing system while DFS stays
# fitted (*DISC / SHIFT+BREAK still work). Slot 15 is left to its aliased BASIC
# socket (IC101) so the machine still boots to BASIC. DNFS 3.02 is deliberately
# NOT used here -- it is 8271-only and silently fatal with the 1770 controller.
add_system_preset(beebium-model-b "model-b-disc-aun-80"
    "Station 80 (AUN, Model B)" "1985"
    DESCRIPTION "Econet over AUN (UDP), ANFS + DFS"
    --fdc acorn-1770
    --sideways 13:rom:acorn-dfs_2_26.rom
    --sideways 14:rom:acorn-anfs_4_18.rom
    --station 80 --aun)

# Tube coprocessor presets, so users can try the second processors (the machine
# config UI has no Tube axis yet). Each is its disc-capable sibling plus a Tube
# coprocessor plugin; both boot to the Tube banner (the DFS ROM carries the Tube
# host code). The 3 MHz / 4 MHz speeds are inherent to the variants. Generated
# with add_plugin_preset (depends on the coprocessor plugin so it is deployed
# before create-preset resolves --tube-*). The plugin targets exist only when the
# servers are built (src/extensions is configured before src/server).
if(TARGET beebium_ext_acorn_65c02_plugin)
    add_plugin_preset(beebium-model-b beebium_ext_acorn_65c02_plugin
        "model-b-disc-65c02-copro"
        "Model B (Disc) + 65C02 Second Processor (3 MHz)" "1984"
        DESCRIPTION "Model B disc + 65C02 second processor (3 MHz)"
        THUMBNAIL_DELAY 6
        --fdc acorn-1770 --sideways 14:rom:acorn-dfs_2_26.rom --tube-65c02)
endif()
if(TARGET beebium_ext_acorn_65c102_plugin)
    add_plugin_preset(beebium-model-b-plus-128k beebium_ext_acorn_65c102_plugin
        "model-b-plus-128k-65c102-copro"
        "Model B+ 128K + 65C102 Co-processor (4 MHz)" "1986"
        DESCRIPTION "128K B+ + 65C102 co-processor (4 MHz)"
        THUMBNAIL_DELAY 6
        --tube-65c102)
endif()

# Econet-over-Piconet preset. Piconet is a plugin, so this uses add_plugin_preset
# (depends on the plugin so it is deployed before create-preset resolves
# --piconet). No device_path is baked in: the extension discovers the connected
# Piconet at boot, and on a host with none attached it boots cleanly with no
# network (create-preset generation and the thumbnail capture never need the
# device). Slot layout matches the AUN preset: ANFS(14) outranks DFS(13) so
# ANFS is the default filing system, BASIC stays in slot 15.
if(TARGET beebium_ext_piconet_plugin)
    add_plugin_preset(beebium-model-b beebium_ext_piconet_plugin
        "model-b-disc-eco-81"
        "Station 81 (Piconet, Model B)" "1985"
        DESCRIPTION "Econet via Piconet USB, ANFS + DFS"
        --fdc acorn-1770
        --sideways 13:rom:acorn-dfs_2_26.rom
        --sideways 14:rom:acorn-anfs_4_18.rom
        --station 81 --piconet)
endif()

# Install rules
install(TARGETS
        beebium-model-b
        beebium-model-b-plus
        beebium-model-b-plus-128k
        beebium-model-b-romram
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${BEEBIUM_ROM_DIR}/
    DESTINATION share/beebium/roms
    FILES_MATCHING PATTERN "*.rom"
)
install(DIRECTORY ${PRESETS_OUTPUT_DIR}/
    DESTINATION share/beebium/presets
    FILES_MATCHING
        PATTERN "*.preset.beebium"
        PATTERN "*.thumbnail.png"
)
