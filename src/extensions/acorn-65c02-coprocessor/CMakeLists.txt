# Acorn 65C02 3 MHz second processor extension (Tube co-processor).
#
# Built two ways from the same source: a STATIC library that the C++ tests
# link directly to construct the extension, and a SHARED-object plugin loaded
# from <exe-dir>/extensions/acorn-65c02-coprocessor/ at runtime like every
# other peripheral. The server reaches it only through the abstract
# CoprocessorExtension interface, so it carries no server link-time knowledge
# and is no longer a built-in.

# Static library (for tests that link the extension directly).
add_library(beebium_ext_acorn_65c02 STATIC
    SecondProcessor65C02Extension.cpp
)

target_include_directories(beebium_ext_acorn_65c02
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/src/server/include
)

target_link_libraries(beebium_ext_acorn_65c02
    PUBLIC beebium_core
)

# Shared-object plugin for the 3 MHz 65C02. It links the static library above
# (so SecondProcessor65C02Extension is compiled exactly once and shared with the
# 65C102 plugin) plus beebium_extension_api, the single shared copy of the
# abstract interfaces the server dynamic_casts to. The static library brings the
# Tube bridge, parasite runner, parasite CPU and 6502 core transitively via
# beebium_core. The plugin does NOT link beebium_service: the debugger lives
# entirely in the server now, so it carries no gRPC or proto descriptors.
# plugin_entry.cpp constructs the extension with the default 3/2 ratio.
if(BEEBIUM_BUILD_SERVICE)
    add_library(beebium_ext_acorn_65c02_plugin SHARED
        plugin_entry.cpp
    )

    target_link_libraries(beebium_ext_acorn_65c02_plugin
        PRIVATE
            beebium_ext_acorn_65c02
            beebium_extension_api
    )

    set_target_properties(beebium_ext_acorn_65c02_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "acorn-65c02-coprocessor"
        # beebium_create_extension is exported explicitly via BEEBIUM_PLUGIN_EXPORT;
        # everything else in the plugin's own translation units stays hidden.
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )

    beebium_finalize_plugin(TARGET beebium_ext_acorn_65c02_plugin NAME acorn-65c02-coprocessor)
endif()

# Copy the Tube client ROM to the build output so the extension finds it at
# runtime relative to the server executable. RomPaths::find_rom searches a
# roms/ directory near the executable (and $BEEBIUM_ROM_DIR, and the installed
# ../share/beebium/roms), independent of where the plugin itself lives -- see
# the lookup order documented in SecondProcessor65C02Extension.cpp::load_rom.
# The server installs the whole rom set into share/beebium/roms, so an
# installed plugin resolves the ROM there.
file(GLOB EXTENSION_ROMS "${CMAKE_CURRENT_SOURCE_DIR}/roms/*.rom")
foreach(ROM_FILE ${EXTENSION_ROMS})
    get_filename_component(ROM_NAME ${ROM_FILE} NAME)
    configure_file(${ROM_FILE}
        ${CMAKE_BINARY_DIR}/roms/${ROM_NAME} COPYONLY)
endforeach()
