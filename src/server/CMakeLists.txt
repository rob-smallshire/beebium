# beebium server executables - one per machine type

# Common include directory for server templates
set(SERVER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# What a target including the server headers (include/beebium/server) needs
# beyond them: the servers and the tests of those headers link it.
add_library(beebium_server_headers INTERFACE)
target_include_directories(beebium_server_headers INTERFACE ${SERVER_INCLUDE_DIR})
target_link_libraries(beebium_server_headers INTERFACE beebium_zip)

# Presets output directory. The install() rules below read presets from here,
# so it MUST be a fixed, config-independent path: install(DIRECTORY) does not
# evaluate generator expressions. Presets are generated straight into this
# directory (below), and on multi-config generators are also copied beside the
# per-config executable for the server's exe-relative discovery from the build
# tree.
set(PRESETS_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/presets")
file(MAKE_DIRECTORY ${PRESETS_OUTPUT_DIR})

# Bundled discs: a disc a system preset carries, committed compressed
# (discs/bundled/<id>.tar.xz, ~55 KB for the 99%-sparse L3FS image, carrying
# <id>.dat + <id>.dsc) and extracted into the preset as it is put together,
# so it ships inside the preset's archive and nowhere else.
# l3fs-v1_26b is the auto-starting revision (see packaging/discs/build-l3fs-image.sh).
set(BUNDLED_DISCS_SRC_DIR "${CMAKE_SOURCE_DIR}/discs/bundled")
set(BUNDLED_DISC_IDS l3fs-v1_26b)

# Set OUT_VAR to the committed tarball of each bundled disc that a preset's
# create-preset arguments name. The preset carries the disc: its master is
# extracted from the tarball into the preset before it is archived, so the
# preset depends on the tarball itself -- derived from the arguments, not
# listed by hand.
function(bundled_disc_tarballs_named OUT_VAR)
    set(tarballs "")
    foreach(disc_id ${BUNDLED_DISC_IDS})
        foreach(arg ${ARGN})
            string(FIND "${arg}" "${disc_id}." position)
            if(NOT position EQUAL -1)
                list(APPEND tarballs "${BUNDLED_DISCS_SRC_DIR}/${disc_id}.tar.xz")
                break()
            endif()
        endforeach()
    endforeach()
    set(${OUT_VAR} "${tarballs}" PARENT_SCOPE)
endfunction()

# Set OUT_VAR to the commands that extract each of `tarballs` into the
# preset directory `source_dirpath`, where the preset names it by bare name.
function(bundled_disc_extract_commands OUT_VAR source_dirpath)
    set(commands "")
    foreach(tarball ${ARGN})
        list(APPEND commands
            COMMAND ${CMAKE_COMMAND}
                -DTARBALL=${tarball}
                -DOUTDIR=${source_dirpath}
                -P ${CMAKE_SOURCE_DIR}/cmake/DecompressBundledDisc.cmake)
    endforeach()
    set(${OUT_VAR} "${commands}" PARENT_SCOPE)
endfunction()

# The per-user directory every build-time create-preset and capture runs
# with: the build's own, so a bare name a preset gives is never found in the
# developer's per-user discs/ or roms/. They run in this directory's binary
# directory (add_custom_command's default), which holds no disc or ROM images,
# so the working-directory step of the search finds nothing there either.
set(PRESET_USER_DIRPATH "${CMAKE_CURRENT_BINARY_DIR}/user-state")

# Where a system preset is put together before it is archived: a directory
# laid out as its archive (preset.json, then the captured thumbnail.png).
set(PRESET_SOURCES_DIR "${CMAKE_CURRENT_BINARY_DIR}/preset-sources")

# Generate a system preset at build time, using the same create-preset code
# path that users create their own presets with, as <id>.beebium-preset: the
# preset is created unpacked, booted to capture its thumbnail into it, and
# exported as the archive. Extra create-preset arguments (e.g. --fdc,
# --sideways) are forwarded via ARGN.
function(add_system_preset EXECUTABLE PRESET_ID PRESET_NAME RELEASE_DATE)
    # DESCRIPTION is a one-value keyword forwarded as create-preset's
    # --description (the concise one-liner the picker card shows); the rest of
    # ARGN goes to create-preset unchanged.
    cmake_parse_arguments(PRESET "" "DESCRIPTION" "" ${ARGN})
    set(description_args "")
    if(DEFINED PRESET_DESCRIPTION)
        set(description_args --description "${PRESET_DESCRIPTION}")
    endif()
    # A bundled disc the arguments name is extracted into the preset, from
    # its committed tarball (a source file, so always there for POST_BUILD).
    bundled_disc_tarballs_named(disc_tarballs ${PRESET_UNPARSED_ARGUMENTS})
    # Generate into the config-independent PRESETS_OUTPUT_DIR that install()
    # reads. (Writing to $<TARGET_FILE_DIR:...>/presets instead put the presets
    # in a per-config Release/presets on multi-config generators, where the
    # fixed install() source never found them -- so the Windows bundle shipped
    # an empty share/beebium/presets.)
    set(preset_filepath "${PRESETS_OUTPUT_DIR}/${PRESET_ID}.beebium-preset")
    set(source_dirpath "${PRESET_SOURCES_DIR}/${PRESET_ID}")
    bundled_disc_extract_commands(extract_commands "${source_dirpath}" ${disc_tarballs})
    add_custom_command(TARGET ${EXECUTABLE} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PRESETS_OUTPUT_DIR}" "${PRESET_SOURCES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${source_dirpath}"
        COMMAND ${CMAKE_COMMAND} -E env "BEEBIUM_USER_DIRPATH=${PRESET_USER_DIRPATH}"
            $<TARGET_FILE:${EXECUTABLE}> create-preset
            --name "${PRESET_NAME}"
            --release-date "${RELEASE_DATE}"
            ${description_args}
            ${PRESET_UNPARSED_ARGUMENTS}
            --output "${source_dirpath}" --unpack
        ${extract_commands}
        # Capture boots the preset: a disc it carries is copied into the
        # capture's own machine directory, so every build boots a pristine
        # image (deterministic thumbnail).
        COMMAND ${CMAKE_COMMAND} -E env
            "BEEBIUM_USER_DIRPATH=${PRESET_USER_DIRPATH}"
            $<TARGET_FILE:${EXECUTABLE}> capture-screenshot
            --preset "${source_dirpath}"
            --output "${source_dirpath}/thumbnail.png"
            --rom-dir "${BEEBIUM_ROM_DIR}"
            --crop auto
        COMMAND ${CMAKE_COMMAND} -E rm -f "${preset_filepath}"
        COMMAND ${CMAKE_COMMAND} -E env "BEEBIUM_USER_DIRPATH=${PRESET_USER_DIRPATH}"
            $<TARGET_FILE:${EXECUTABLE}> export-preset "${source_dirpath}"
            --output "${preset_filepath}"
        COMMENT "Generating preset ${PRESET_ID}"
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
    # EXTRA_DEPENDS: further coprocessor/peripheral plugin targets, beyond the
    # primary PLUGIN_TARGET, that create-preset/capture need. Bundled discs the
    # arguments name are found by bundled_disc_tarballs_named. CAPTURE_ARGS go
    # to capture-screenshot only, never into the preset: what the thumbnail
    # needs fixed so that every build captures the same image (a clock the
    # screen shows), where the preset itself follows the host.
    cmake_parse_arguments(PRESET "" "THUMBNAIL_DELAY;DESCRIPTION" "EXTRA_DEPENDS;CAPTURE_ARGS"
                          ${ARGN})
    set(capture_duration_args "")
    if(DEFINED PRESET_THUMBNAIL_DELAY)
        set(capture_duration_args --duration ${PRESET_THUMBNAIL_DELAY})
    endif()
    set(description_args "")
    if(DEFINED PRESET_DESCRIPTION)
        set(description_args --description "${PRESET_DESCRIPTION}")
    endif()
    bundled_disc_tarballs_named(disc_tarballs ${PRESET_UNPARSED_ARGUMENTS})
    set(preset_filepath "${PRESETS_OUTPUT_DIR}/${PRESET_ID}.beebium-preset")
    set(source_dirpath "${PRESET_SOURCES_DIR}/${PRESET_ID}")
    bundled_disc_extract_commands(extract_commands "${source_dirpath}" ${disc_tarballs})
    set(copy_commands "")
    if(CMAKE_CONFIGURATION_TYPES)
        set(copy_commands
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${EXECUTABLE}>/presets"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${preset_filepath}"
                "$<TARGET_FILE_DIR:${EXECUTABLE}>/presets/")
    endif()
    # One command makes the archive from nothing, so its OUTPUT is everything
    # preset_<id> needs: the unpacked directory and the thumbnail captured into
    # it are made and consumed within it, never by another target.
    add_custom_command(
        OUTPUT "${preset_filepath}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PRESETS_OUTPUT_DIR}" "${PRESET_SOURCES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${source_dirpath}"
        COMMAND ${CMAKE_COMMAND} -E env "BEEBIUM_USER_DIRPATH=${PRESET_USER_DIRPATH}"
            $<TARGET_FILE:${EXECUTABLE}> create-preset
            --name "${PRESET_NAME}"
            --release-date "${RELEASE_DATE}"
            ${description_args}
            ${PRESET_UNPARSED_ARGUMENTS}
            --output "${source_dirpath}" --unpack
        ${extract_commands}
        # A disc the preset carries boots from a pristine copy in the
        # capture's own machine directory (see add_system_preset).
        COMMAND ${CMAKE_COMMAND} -E env
            "BEEBIUM_USER_DIRPATH=${PRESET_USER_DIRPATH}"
            $<TARGET_FILE:${EXECUTABLE}> capture-screenshot
            --preset "${source_dirpath}"
            --output "${source_dirpath}/thumbnail.png"
            --rom-dir "${BEEBIUM_ROM_DIR}"
            --crop auto
            ${capture_duration_args}
            ${PRESET_CAPTURE_ARGS}
        COMMAND ${CMAKE_COMMAND} -E rm -f "${preset_filepath}"
        COMMAND ${CMAKE_COMMAND} -E env "BEEBIUM_USER_DIRPATH=${PRESET_USER_DIRPATH}"
            $<TARGET_FILE:${EXECUTABLE}> export-preset "${source_dirpath}"
            --output "${preset_filepath}"
        ${copy_commands}
        DEPENDS ${EXECUTABLE} ${PLUGIN_TARGET} ${PRESET_EXTRA_DEPENDS} ${disc_tarballs}
            ${CMAKE_SOURCE_DIR}/cmake/DecompressBundledDisc.cmake
        COMMENT "Generating plugin preset ${PRESET_ID}"
        VERBATIM
    )
    add_custom_target(preset_${PRESET_ID} ALL DEPENDS "${preset_filepath}")
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
            beebium_server_headers
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
add_beebium_server(beebium-model-b-atpl-sidewise main_model_b_atpl_sidewise.cpp)
add_beebium_server(beebium-model-b-watford-rom-ram main_model_b_watford_rom_ram.cpp)
add_beebium_server(beebium-model-b-integra-b main_model_b_integra_b.cpp)

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
    beebium-model-b-atpl-sidewise
    beebium-model-b-watford-rom-ram
    beebium-model-b-integra-b
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
        # Every sideways ROM any shipped preset references (see the add_*_preset
        # --sideways NN:rom:... lines above) must be copied here, or a dev-tree /
        # BEEBIUM_SERVERS_DIRPATH launch of that preset fails with "ROM file not
        # found" (installed bundles ship all *.rom, so this bites the build tree
        # only). Currently: DFS 2.26, ANFS 4.18, ADFS 1.30, plus the Integra-B's
        # IBOS, a machine default ROM. (Tube coprocessor
        # ROMs are NOT here -- they ship inside each coprocessor plugin's roms/
        # directory and load via the extension, not the ROM directory.)
        "${BEEBIUM_ROM_DIR}/acorn-anfs_4_18.rom"
        "${BEEBIUM_ROM_DIR}/acorn-adfs_1_30.rom"
        "${BEEBIUM_ROM_DIR}/computech-ibos_1_26.rom"
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
    --fdc acorn-1770 --sideways slot=14:type=rom:image=acorn-dfs_2_26.rom)
add_system_preset(beebium-model-b-romram "model-b-romram-disc"
    "Model B with ROM/RAM Board (Disc)" "1984"
    DESCRIPTION "Model B, ROM/RAM board, 1770 DFS"
    --fdc acorn-1770 --sideways slot=14:type=rom:image=acorn-dfs_2_26.rom)
# BASIC auto-loads into slot 14 (this machine's default language slot), so the
# DFS goes in slot 13 and slot 15 is the board's sideways RAM.
add_system_preset(beebium-model-b-atpl-sidewise "model-b-atpl-sidewise-disc"
    "Model B with ATPL Sidewise (Disc)" "1984"
    DESCRIPTION "Model B, ATPL Sidewise board, 1770 DFS, 16K sideways RAM in slot 15"
    --fdc acorn-1770 --sideways slot=13:type=rom:image=acorn-dfs_2_26.rom --sideways slot=15:type=ram)
# One preset for the Watford ROM/RAM board: the 128K sideways-RAM configuration
# (all eight DRAM banks 0-7). BASIC auto-loads into slot 15 (this machine's
# default language slot, the board socket the manual recommends); DFS goes in
# board ROM slot 13. Other sizes are a matter of fitting fewer RAM banks via
# --sideways slot=N:type=ram.
add_system_preset(beebium-model-b-watford-rom-ram "model-b-watford-rom-ram-128k"
    "Model B with Watford ROM/RAM (128K)" "1985"
    DESCRIPTION "Model B, Watford ROM/RAM board, 1770 DFS, 128K sideways RAM (banks 0-7)"
    --fdc acorn-1770 --sideways slot=13:type=rom:image=acorn-dfs_2_26.rom
    --sideways slot=0:type=ram --sideways slot=1:type=ram
    --sideways slot=2:type=ram --sideways slot=3:type=ram
    --sideways slot=4:type=ram --sideways slot=5:type=ram
    --sideways slot=6:type=ram --sideways slot=7:type=ram)
# Integra-B: IBOS auto-loads into slot 15 and BASIC into motherboard slot 3
# (this machine's defaults); DFS goes in motherboard slot 1, as in the IBOS
# guide's own *ROMS example. Board RAM banks 4-7 are always fitted.
add_system_preset(beebium-model-b-integra-b "model-b-integra-b-disc"
    "Model B with Integra-B (Disc)" "1988"
    DESCRIPTION "Model B, Integra-B board (IBOS 1.26), 1770 DFS, shadow RAM, RTC"
    --fdc acorn-1770 --sideways slot=1:type=rom:image=acorn-dfs_2_26.rom)
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
    # The name follows the station number in force (#153), so a renumbered
    # machine's title follows it at the next Break.
    --machine-description "Station {econet-station} (AUN, Model B)"
    --fdc acorn-1770
    --sideways slot=13:type=rom:image=acorn-dfs_2_26.rom
    --sideways slot=14:type=rom:image=acorn-anfs_4_18.rom
    # port=0: bind an OS-ephemeral UDP port rather than the fixed 32768, so
    # this station and a file-server station (both AUN) can run on ONE host
    # without a bind conflict. Each advertises its real bound port over
    # _aun._udp and discovers the peer via mDNS -- no fixed port, no map=.
    --station 80 --aun port=0)

# As above but with an automatically-chosen station number (issue #67): the
# server browses _aun._udp at launch and takes the lowest free number in the
# default range, so several clients can be started together with no renumbering
# (the trigger for #68/#148/#155). The name is a template (#153), so each
# machine is titled with the number it chose.
add_system_preset(beebium-model-b "model-b-disc-aun-auto"
    "AUN client, auto station (Model B)" "1985"
    DESCRIPTION "Econet over AUN (UDP), automatic station number, ANFS + DFS"
    --machine-description "Station {econet-station} (AUN, Model B)"
    --fdc acorn-1770
    --sideways slot=13:type=rom:image=acorn-dfs_2_26.rom
    --sideways slot=14:type=rom:image=acorn-anfs_4_18.rom
    --station auto --aun port=0)

# Tube coprocessor presets, so users can try the second processors (the machine
# config UI has no Tube axis yet). Each is its disc-capable sibling plus a Tube
# coprocessor plugin; both boot to the Tube banner (the DFS ROM carries the Tube
# host code). The 3 MHz / 4 MHz speeds are inherent to the variants. Each
# machine's name takes the CPU and clock from the coprocessor's own report
# (#173), rendering to the preset's title. Generated
# with add_plugin_preset (depends on the coprocessor plugin so it is deployed
# before create-preset resolves --tube-*). The plugin targets exist only when the
# servers are built (src/extensions is configured before src/server).
if(TARGET beebium_ext_acorn_65c02_plugin)
    add_plugin_preset(beebium-model-b beebium_ext_acorn_65c02_plugin
        "model-b-disc-65c02-copro"
        "Model B (Disc), 65C02 Copro (3 MHz)" "1984"
        DESCRIPTION "Model B disc, 65C02 copro (3 MHz)"
        THUMBNAIL_DELAY 6
        --machine-description "Model B (Disc), {coprocessor-cpu} Copro ({coprocessor-clock})"
        --fdc acorn-1770 --sideways slot=14:type=rom:image=acorn-dfs_2_26.rom --tube-65c02)
endif()
if(TARGET beebium_ext_acorn_65c102_plugin)
    add_plugin_preset(beebium-model-b-plus-128k beebium_ext_acorn_65c102_plugin
        "model-b-plus-128k-65c102-copro"
        "Model B+ 128K, 65C102 Copro (4 MHz)" "1986"
        DESCRIPTION "128K B+, 65C102 copro (4 MHz)"
        THUMBNAIL_DELAY 6
        --machine-description "Model B+ 128K, {coprocessor-cpu} Copro ({coprocessor-clock})"
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
        --machine-description "Station {econet-station} (Piconet, Model B)"
        --fdc acorn-1770
        --sideways slot=13:type=rom:image=acorn-dfs_2_26.rom
        --sideways slot=14:type=rom:image=acorn-anfs_4_18.rom
        --station 81 --piconet)
endif()

# Acorn Level 3 File Server preset. A full server machine: a 65C02 second
# processor (3 MHz) runs the L3FS software off a bundled SCSI hard disc, with
# ADFS as the DEFAULT filing system (ANFS in slot 9, ADFS in the higher-priority
# slot 10) so the machine boots into ADFS to load the server, ANFS present for
# the Econet side, station 254 (the conventional file-server station) over AUN
# so clients find it via _aun._udp discovery, and the RTC in its L3-v1.26 layout
# for date/time. The disc image is referenced by its bare, version-named
# filename and copied into each launch's machine directory (see DiscPaths);
# the shipped master is never modified. Needs the SCSI adapter + hard-disc + RTC
# plugins deployed before create-preset/capture run, hence EXTRA_DEPENDS; the
# disc it names is depended on automatically. Advertising (--advertise) is a
# launch-time flag, not preset config, so the launcher enables mDNS
# advertisement, not the preset.
if(TARGET beebium_ext_acorn_65c02_plugin
   AND TARGET beebium_ext_acorn_scsi_plugin
   AND TARGET beebium_ext_scsi_hard_disc_plugin
   AND TARGET beebium_ext_acorn_rtc_plugin)
    add_plugin_preset(beebium-model-b beebium_ext_acorn_65c02_plugin
        "model-b-l3fs-aun"
        "Station 254 (L3 File Server, Model B)" "1985"
        DESCRIPTION "Level 3 File Server: ADFS + ANFS, SCSI disc, 65C02 copro"
        # The image auto-starts the file server on boot (its !BOOT chains a
        # soft-key StartFS), so --auto-boot lets a cold boot reach a SERVING
        # server unattended; the thumbnail then captures "Starting - Ready".
        # THUMBNAIL_DELAY covers the parasite's SCSI bitmap read + FS start.
        THUMBNAIL_DELAY 30
        --machine-description "L3 File Server, station {econet-station}"
        --auto-boot
        --fdc acorn-1770
        --sideways slot=9:type=rom:image=acorn-anfs_4_18.rom
        --sideways slot=10:type=rom:image=acorn-adfs_1_30.rom
        # port=0: OS-ephemeral UDP port (not fixed 32768) so this server and a
        # client station (both AUN) coexist on one host with no bind conflict;
        # the real bound port is advertised over _aun._udp for mDNS discovery.
        --station 254 --aun port=0
        # The clock has an id so that the capture can start it at a fixed time
        # (CAPTURE_ARGS below) without the preset doing so.
        --acorn-rtc id=clock:layout=7bit-year-in-r7
        --acorn-scsi
        # The disc has an id so that a machine made from the preset can be
        # given another image in its place (--scsi-hdd id=disc:image=...).
        --scsi-hdd scsi-id=0:image=l3fs-v1_26b.dat:id=disc
        --tube-65c02
        EXTRA_DEPENDS
            beebium_ext_acorn_scsi_plugin
            beebium_ext_scsi_hard_disc_plugin
            beebium_ext_acorn_rtc_plugin
        # The file server shows the clock's date and time once ready.
        CAPTURE_ARGS --acorn-rtc id=clock:time=1985-03-15T1030)
endif()

# Install rules
install(TARGETS
        beebium-model-b
        beebium-model-b-plus
        beebium-model-b-plus-128k
        beebium-model-b-romram
        beebium-model-b-atpl-sidewise
        beebium-model-b-watford-rom-ram
        beebium-model-b-integra-b
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${BEEBIUM_ROM_DIR}/
    DESTINATION share/beebium/roms
    FILES_MATCHING PATTERN "*.rom"
)
install(DIRECTORY ${PRESETS_OUTPUT_DIR}/
    DESTINATION share/beebium/presets
    FILES_MATCHING
        PATTERN "*.beebium-preset"
)
