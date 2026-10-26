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

# Static library (built-in extension, and for tests)
add_library(beebium_ext_hybrid_music_5000 STATIC
    HybridMusic5000.cpp
)

target_include_directories(beebium_ext_hybrid_music_5000
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_hybrid_music_5000
    PUBLIC
        beebium_music5000_core
        beebium_core
        beebium_extension_api
)

# Shared library (plugin extension)
if(BEEBIUM_BUILD_SERVICE)
    add_library(beebium_ext_hybrid_music_5000_plugin SHARED
        HybridMusic5000.cpp
        plugin_entry.cpp
    )

    target_include_directories(beebium_ext_hybrid_music_5000_plugin
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(beebium_ext_hybrid_music_5000_plugin
        PRIVATE
            beebium_music5000_core
            beebium_extension_api
    )

    set_target_properties(beebium_ext_hybrid_music_5000_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "hybrid-music-5000"
    )

    beebium_finalize_plugin(TARGET beebium_ext_hybrid_music_5000_plugin NAME hybrid-music-5000)
endif()
