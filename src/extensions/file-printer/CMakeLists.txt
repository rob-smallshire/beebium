# File printer (built-in extension).
#
# Spools everything the BBC sends to its printer port into a file on the
# host: a CentronicsReceiver attached through the PrinterPort handle, drained
# by a sink thread into a std::ofstream. Builds as a STATIC library linked
# directly into the server.
#
# Built-in rather than a plugin for the same reasons as host-serial: the
# --file-printer flag must be recognised without a plugin directory, presets
# can name it unconditionally, and it is the test oracle for everything else
# on the printer port. See BuiltinExtensions.hpp.
#
# When the service layer is being built, compiles the file_printer.proto
# MESSAGES (not the gRPC stub); the typed API (GetStatus / SetOnline) is
# served through the core's ExtensionRpc channel by a hand-written
# FilePrinterDispatcher, so this library links protobuf but NOT gRPC.

set(FILE_PRINTER_SOURCES
    FilePrinterExtension.cpp
    FilePrinterUi.cpp
)

if(BEEBIUM_BUILD_SERVICE)
    beebium_compile_proto(
        TARGET beebium_ext_file_printer
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/file_printer.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )
    # Messages only: the dispatcher (not a gRPC service) (de)serializes these.
    list(APPEND FILE_PRINTER_SOURCES
        ${beebium_ext_file_printer_PROTO_SRCS}
    )
endif()

add_library(beebium_ext_file_printer STATIC ${FILE_PRINTER_SOURCES})

target_include_directories(beebium_ext_file_printer
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_file_printer
    PUBLIC
        beebium_core
        # FilePrinterUi.cpp composes extension_ui.pb.h View / DispatchRequest types.
        beebium_extension_ui_proto
)

if(BEEBIUM_BUILD_SERVICE)
    target_include_directories(beebium_ext_file_printer
        PUBLIC ${beebium_ext_file_printer_OUT_DIR}
    )
    target_compile_definitions(beebium_ext_file_printer PUBLIC BEEBIUM_BUILD_SERVICE)
    target_link_libraries(beebium_ext_file_printer
        PUBLIC
            protobuf::libprotobuf
            beebium_extension_api
    )
endif()
