# AUN UDP Econet transport (built-in extension).
#
# Builds as a STATIC library linked directly into the server. The
# rationale for keeping AUN built-in rather than converting it to a
# plugin is primarily CLI-test ergonomics: many tests invoke
# parse_start_arguments with a synthetic argv (argv[0] = "beebium")
# that cannot auto-discover plugins via exe-parent/extensions, so
# --aun needs to be recognised without a plugin dir. A future
# test-side refactor could let CLI tests point at a plugin dir and
# unblock converting AUN; until then, AUN remains in
# BuiltinExtensions::entries() alongside acorn-65c02-coprocessor.
#
# When the service layer is being built, also compiles the aun.proto
# MESSAGES (not the gRPC stub). The AUN-specific RPCs (peer table
# management, cable plug, port status) are served through the core's
# ExtensionRpc channel via a hand-written AunDispatcher, so this library
# links protobuf but NOT gRPC. See docs/discussion/extension-rpc-channel.md.

set(AUN_SOURCES
    AunEconetTransportExtension.cpp
    AunDiscoveryAnnouncer.cpp
    AunDiscoverySubscriber.cpp
    AunMapFile.cpp
    AunPeerSet.cpp
    AunUi.cpp
)

if(BEEBIUM_BUILD_SERVICE)
    beebium_compile_proto(
        TARGET beebium_ext_aun
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/aun.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )
    # Messages only: the dispatcher (de)serializes these. The generated
    # .grpc.pb.cc stub is deliberately NOT compiled -- that is what would
    # pull gRPC in.
    list(APPEND AUN_SOURCES
        ${beebium_ext_aun_PROTO_SRCS}
    )
endif()

add_library(beebium_ext_aun STATIC ${AUN_SOURCES})

target_include_directories(beebium_ext_aun
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

if(BEEBIUM_BUILD_SERVICE)
    target_include_directories(beebium_ext_aun
        PUBLIC ${beebium_ext_aun_OUT_DIR}
    )
    target_compile_definitions(beebium_ext_aun PUBLIC BEEBIUM_BUILD_SERVICE)
endif()

target_link_libraries(beebium_ext_aun
    PUBLIC
        beebium_core
        beebium_discovery
)

# Threaded through into AunDiscoveryAnnouncer's TXT record so
# discovered peers can tell which Beebium release they're talking to.
target_compile_definitions(beebium_ext_aun PRIVATE
    BEEBIUM_VERSION="${PROJECT_VERSION}"
)

if(BEEBIUM_BUILD_SERVICE)
    # protobuf for the aun.proto messages; beebium_extension_api for the
    # ExtensionRpcDispatcher ABI the AunDispatcher implements. No gRPC.
    target_link_libraries(beebium_ext_aun
        PUBLIC
            protobuf::libprotobuf
            beebium_extension_api
    )
endif()
