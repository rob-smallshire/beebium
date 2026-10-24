# Job file printer (built-in extension).
#
# Writes what the BBC prints as one file per print job, a job ending after a
# quiet period in emulated time: a CentronicsReceiver attached through the
# PrinterPort handle, drained by a sink thread into a file per job. Builds as
# a STATIC library linked directly into the server.
#
# Built-in for the same reasons as file-printer: --job-file-printer must be
# recognised without a plugin directory, and presets can name it
# unconditionally. See BuiltinExtensions.hpp.
#
# When the service layer is being built, compiles the job_file_printer.proto
# MESSAGES (not the gRPC stub); the typed API (GetStatus) is
# served through the core's ExtensionRpc channel by a hand-written
# JobFilePrinterDispatcher, so this library links protobuf but NOT gRPC.

set(JOB_FILE_PRINTER_SOURCES
    JobFilePrinterExtension.cpp
    JobFilePrinterUi.cpp
)

if(BEEBIUM_BUILD_SERVICE)
    beebium_compile_proto(
        TARGET beebium_ext_job_file_printer
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/job_file_printer.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )
    # Messages only: the dispatcher (not a gRPC service) (de)serializes these.
    list(APPEND JOB_FILE_PRINTER_SOURCES
        ${beebium_ext_job_file_printer_PROTO_SRCS}
    )
endif()

add_library(beebium_ext_job_file_printer STATIC ${JOB_FILE_PRINTER_SOURCES})

target_include_directories(beebium_ext_job_file_printer
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_job_file_printer
    PUBLIC
        beebium_core
        # JobFilePrinterUi.cpp composes extension_ui.pb.h View / DispatchRequest types.
        beebium_extension_ui_proto
)

if(BEEBIUM_BUILD_SERVICE)
    target_include_directories(beebium_ext_job_file_printer
        PUBLIC ${beebium_ext_job_file_printer_OUT_DIR}
    )
    target_compile_definitions(beebium_ext_job_file_printer PUBLIC BEEBIUM_BUILD_SERVICE)
    target_link_libraries(beebium_ext_job_file_printer
        PUBLIC
            protobuf::libprotobuf
            beebium_extension_api
    )
endif()
