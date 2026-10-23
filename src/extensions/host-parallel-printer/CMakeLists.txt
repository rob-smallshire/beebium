# Host printer port (host-parallel-printer): sends everything the BBC prints
# to a printer device on the host, such as /dev/usb/lp0, /dev/lp0 or LPT1. A
# CentronicsReceiver attached through the PrinterPort handle is drained into
# the device by PrinterDevice, a small platform shim (PrinterDevicePosix.cpp
# or PrinterDeviceWin32.cpp) over the plain device-file calls: a printer
# needs none of the serial setup in beebium/serial. Ships as a
# dynamically-loaded plugin, discovered from <exe-dir>/extensions/.
#
# When the service layer is being built, compiles the
# host_parallel_printer.proto MESSAGES (not the gRPC stub); the typed API
# (GetStatus) is served through the core's ExtensionRpc channel by a
# hand-written HostParallelPrinterDispatcher, so the plugin links protobuf but
# NOT gRPC. See docs/discussion/extension-rpc-channel.md.

set(HOST_PARALLEL_PRINTER_SOURCES
    HostParallelPrinterExtension.cpp
    HostParallelPrinterSink.cpp
    HostParallelPrinterUi.cpp
    PrinterDevice.cpp
)
if(WIN32)
    list(APPEND HOST_PARALLEL_PRINTER_SOURCES PrinterDeviceWin32.cpp)
else()
    list(APPEND HOST_PARALLEL_PRINTER_SOURCES PrinterDevicePosix.cpp)
endif()

if(BEEBIUM_BUILD_SERVICE)
    beebium_compile_proto(
        TARGET beebium_ext_host_parallel_printer
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/host_parallel_printer.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Messages only, built once for both the static and the shared target.
    add_library(beebium_ext_host_parallel_printer_proto OBJECT
        ${beebium_ext_host_parallel_printer_PROTO_SRCS}
    )
    target_include_directories(beebium_ext_host_parallel_printer_proto
        PUBLIC ${beebium_ext_host_parallel_printer_OUT_DIR}
    )
    target_link_libraries(beebium_ext_host_parallel_printer_proto
        PUBLIC protobuf::libprotobuf
    )
    set_target_properties(beebium_ext_host_parallel_printer_proto PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

# Static library -- linked by the unit tests, which construct the sink and
# the extension directly.
add_library(beebium_ext_host_parallel_printer STATIC ${HOST_PARALLEL_PRINTER_SOURCES})

target_include_directories(beebium_ext_host_parallel_printer
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_host_parallel_printer
    PUBLIC
        beebium_core
        # HostParallelPrinterUi.cpp composes extension_ui.pb.h View / DispatchRequest types.
        beebium_extension_ui_proto
)
if(WIN32)
    # PrinterDeviceWin32.cpp enumerates USB printers through SetupAPI.
    target_link_libraries(beebium_ext_host_parallel_printer PUBLIC setupapi)
endif()

if(BEEBIUM_BUILD_SERVICE)
    target_compile_definitions(beebium_ext_host_parallel_printer PUBLIC BEEBIUM_BUILD_SERVICE)
    target_link_libraries(beebium_ext_host_parallel_printer
        PUBLIC
            beebium_ext_host_parallel_printer_proto
            beebium_extension_api
    )

    # Shared library (the plugin). Deployed to <exe-dir>/extensions/.
    add_library(beebium_ext_host_parallel_printer_plugin SHARED
        ${HOST_PARALLEL_PRINTER_SOURCES}
        plugin_entry.cpp
    )
    target_include_directories(beebium_ext_host_parallel_printer_plugin
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(beebium_ext_host_parallel_printer_plugin
        PRIVATE BEEBIUM_BUILD_SERVICE)
    target_link_libraries(beebium_ext_host_parallel_printer_plugin
        PRIVATE
            beebium_core
            beebium_extension_api
            beebium_ext_host_parallel_printer_proto
            beebium_extension_ui_proto
    )
    set_target_properties(beebium_ext_host_parallel_printer_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "host-parallel-printer"
    )
    if(WIN32)
        target_link_libraries(beebium_ext_host_parallel_printer_plugin PRIVATE setupapi)
    endif()
    beebium_finalize_plugin(TARGET beebium_ext_host_parallel_printer_plugin
                            NAME host-parallel-printer)
endif()
