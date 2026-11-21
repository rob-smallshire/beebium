# Acorn ROM Cartridge (built-in extension).
#
# A cartridge of one or two 16K ROMs in one of the Master 128's cartridge
# slots (cartridge-rear, cartridge-front). Builds as a STATIC library linked
# directly into the server, like the joystick, so --acorn-rom-cartridge is
# recognised without a plugin directory. See BuiltinExtensions.hpp.

add_library(beebium_ext_acorn_rom_cartridge STATIC
    AcornRomCartridgeExtension.cpp
)

target_include_directories(beebium_ext_acorn_rom_cartridge
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_acorn_rom_cartridge
    PUBLIC
        beebium_core
)
