# Hybrid Music 5000 / Music 3000 synthesiser extension

# The synthesiser's DSP, with no framework dependencies. Used by unit tests
# and linked into the extension.
add_library(beebium_music5000_core STATIC
    Music5000Synth.cpp
)

target_include_directories(beebium_music5000_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(beebium_music5000_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
