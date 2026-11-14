# Joystick (built-in extension).
#
# Two analogue joysticks with a fire button each, in the analogue port: an
# AnaloguePortDevice the uPD7002 and the System VIA read, and a Controller a
# client sets through ControllerService. Builds as a STATIC library linked
# directly into the server, for the same reasons as file-printer: --joystick
# must be recognised without a plugin directory. See BuiltinExtensions.hpp.

add_library(beebium_ext_joystick STATIC
    JoystickExtension.cpp
)

target_include_directories(beebium_ext_joystick
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_joystick
    PUBLIC
        beebium_core
)
