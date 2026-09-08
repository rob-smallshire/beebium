# Acorn 65C102 4 MHz second processor (Tube co-processor).
#
# A second plugin built from the SAME source as the 65C02: it links the
# acorn-65c02-coprocessor static library (so SecondProcessor65C02Extension is
# compiled exactly once) and constructs it with the 65C102's 2/1 clock ratio in
# plugin_entry.cpp. The two are software-identical -- same Tube client ROM, same
# 64 KB RAM -- so only the clock and identity differ.
#
# This is a separate directory rather than one library exporting two manifests
# because the plugin loader reads one manifest.json per directory.
if(BEEBIUM_BUILD_SERVICE)
    add_library(beebium_ext_acorn_65c102_plugin SHARED
        plugin_entry.cpp
    )

    target_link_libraries(beebium_ext_acorn_65c102_plugin
        PRIVATE
            beebium_ext_acorn_65c02
            beebium_extension_api
    )

    set_target_properties(beebium_ext_acorn_65c102_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "acorn-65c102-coprocessor"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )

    beebium_finalize_plugin(TARGET beebium_ext_acorn_65c102_plugin NAME acorn-65c102-coprocessor)
endif()
