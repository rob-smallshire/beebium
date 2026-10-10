# Acorn User Port RTC extension (SAF3019P)

# Core library: chip emulation + time parsing, no gRPC dependencies.
# Used by unit tests and linked into the full extension.
add_library(beebium_rtc_core STATIC
    AcornRtcConfig.cpp
    Saf3019p.cpp
    TimeParser.cpp
)

target_include_directories(beebium_rtc_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

# AcornRtcConfig reports through the extension API's ConfigDiagnostic.
target_link_libraries(beebium_rtc_core PUBLIC beebium_extension_api)

# Full extension with gRPC service (depends on service layer)
if(BEEBIUM_BUILD_SERVICE)
    # Compile proto -- use an OBJECT library so the generated sources are
    # built exactly once, avoiding MSBuild parallel-build races on Windows
    # where both the static and shared targets would try to generate the
    # same .pb.cc file simultaneously.
    beebium_compile_proto(
        TARGET beebium_ext_acorn_rtc
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/acorn_rtc.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Messages only: the dispatcher (de)serializes these. The generated
    # .grpc.pb.cc stub is deliberately NOT compiled -- that (and the macOS
    # headers-only Option 2 linkage it needed) is what would pull gRPC in. The
    # RTC's typed API is served over the core's ExtensionRpc channel instead.
    # See docs/discussion/extension-rpc-channel.md.
    add_library(beebium_ext_acorn_rtc_proto OBJECT
        ${beebium_ext_acorn_rtc_PROTO_SRCS}
    )

    target_include_directories(beebium_ext_acorn_rtc_proto
        PUBLIC ${beebium_ext_acorn_rtc_OUT_DIR}
    )

    target_link_libraries(beebium_ext_acorn_rtc_proto
        PUBLIC protobuf::libprotobuf
    )

    set_target_properties(beebium_ext_acorn_rtc_proto PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )

    # Static library (built-in extension)
    add_library(beebium_ext_acorn_rtc STATIC
        AcornRtcExtension.cpp
        AcornRtcUi.cpp
    )

    target_include_directories(beebium_ext_acorn_rtc
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(beebium_ext_acorn_rtc
        PUBLIC
            beebium_rtc_core
            beebium_core
            beebium_extension_api
            beebium_ext_acorn_rtc_proto
            # AcornRtcUi.cpp pulls in extension_ui.pb.h for the
            # View / DispatchRequest types it composes.
            beebium_extension_ui_proto
    )

    # Shared library (plugin extension)
    add_library(beebium_ext_acorn_rtc_plugin SHARED
        AcornRtcExtension.cpp
        AcornRtcUi.cpp
        plugin_entry.cpp
    )

    target_include_directories(beebium_ext_acorn_rtc_plugin
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(beebium_ext_acorn_rtc_plugin
        PRIVATE
            beebium_rtc_core
            beebium_extension_api
            beebium_ext_acorn_rtc_proto
            # AcornRtcUi.cpp pulls in extension_ui.pb.h for the
            # View / DispatchRequest types it composes.
            beebium_extension_ui_proto
    )

    set_target_properties(beebium_ext_acorn_rtc_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "acorn-rtc"
    )

    beebium_finalize_plugin(TARGET beebium_ext_acorn_rtc_plugin NAME acorn-rtc)
endif()
