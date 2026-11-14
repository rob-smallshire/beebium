# Voltmace Delta 14B/1 adapter (built-in extension).
#
# Voltmace's adapter box with two Delta 14B handsets, in the analogue port
# and the user port together: an AnaloguePortDevice (the four pots and the
# fire lines), a UserPortDevice (the 3 by 4 keypad matrix behind a 74LS157
# select) and a Controller a client sets through ControllerService. Built-in
# for the same reason as the joystick: --voltmace-delta-14b must be
# recognised without a plugin directory. See BuiltinExtensions.hpp and
# docs/analogue-port.md.

add_library(beebium_ext_voltmace_delta_14b STATIC
    VoltmaceDelta14bExtension.cpp
)

target_include_directories(beebium_ext_voltmace_delta_14b
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_voltmace_delta_14b
    PUBLIC
        beebium_core
)
