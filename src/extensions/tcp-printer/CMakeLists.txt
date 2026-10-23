# Network printer (tcp-printer): sends everything the BBC prints to a raw TCP
# printer port, such as a JetDirect printer on port 9100 or a printer
# emulator. A CentronicsReceiver attached through the PrinterPort handle is
# drained into a net::TcpClientSerialPort, which is why the plugin links
# beebium_core, as ip232-serial does. Ships as a dynamically-loaded plugin,
# discovered from <exe-dir>/extensions/.
#
# When the service layer is being built, compiles the tcp_printer.proto
# MESSAGES (not the gRPC stub); the typed API (GetStatus) is served through
# the core's ExtensionRpc channel by a hand-written TcpPrinterDispatcher, so
# the plugin links protobuf but NOT gRPC. See
# docs/discussion/extension-rpc-channel.md.

set(TCP_PRINTER_SOURCES
    TcpPrinterExtension.cpp
    TcpPrinterSink.cpp
    TcpPrinterUi.cpp
)

if(BEEBIUM_BUILD_SERVICE)
    beebium_compile_proto(
        TARGET beebium_ext_tcp_printer
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/tcp_printer.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Messages only, built once for both the static and the shared target.
    add_library(beebium_ext_tcp_printer_proto OBJECT
        ${beebium_ext_tcp_printer_PROTO_SRCS}
    )
    target_include_directories(beebium_ext_tcp_printer_proto
        PUBLIC ${beebium_ext_tcp_printer_OUT_DIR}
    )
    target_link_libraries(beebium_ext_tcp_printer_proto
        PUBLIC protobuf::libprotobuf
    )
    set_target_properties(beebium_ext_tcp_printer_proto PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

# Static library -- linked by the unit tests, which construct the sink and
# the extension directly.
add_library(beebium_ext_tcp_printer STATIC ${TCP_PRINTER_SOURCES})

target_include_directories(beebium_ext_tcp_printer
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_tcp_printer
    PUBLIC
        beebium_core
        # TcpPrinterUi.cpp composes extension_ui.pb.h View / DispatchRequest types.
        beebium_extension_ui_proto
)

if(BEEBIUM_BUILD_SERVICE)
    target_compile_definitions(beebium_ext_tcp_printer PUBLIC BEEBIUM_BUILD_SERVICE)
    target_link_libraries(beebium_ext_tcp_printer
        PUBLIC
            beebium_ext_tcp_printer_proto
            beebium_extension_api
    )

    # Shared library (the plugin). Deployed to <exe-dir>/extensions/.
    add_library(beebium_ext_tcp_printer_plugin SHARED
        ${TCP_PRINTER_SOURCES}
        plugin_entry.cpp
    )
    target_include_directories(beebium_ext_tcp_printer_plugin
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(beebium_ext_tcp_printer_plugin PRIVATE BEEBIUM_BUILD_SERVICE)
    target_link_libraries(beebium_ext_tcp_printer_plugin
        PRIVATE
            beebium_core
            beebium_extension_api
            beebium_ext_tcp_printer_proto
            beebium_extension_ui_proto
    )
    set_target_properties(beebium_ext_tcp_printer_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "tcp-printer"
    )
    beebium_finalize_plugin(TARGET beebium_ext_tcp_printer_plugin NAME tcp-printer)
endif()
