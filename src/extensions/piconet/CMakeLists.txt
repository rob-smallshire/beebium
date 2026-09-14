# Piconet USB-CDC Econet transport extension.
#
# The platform SerialPort implementations (PosixSerialPort/Win32SerialPort)
# were promoted to beebium core (beebium::serial); the Piconet headers under
# include/beebium/econet/piconet/ are now thin aliasing shims. beebium_core is
# linked PUBLIC below, so the symbols resolve without compiling them here.
set(PICONET_SERIAL_SRCS)

# Core protocol library: Piconet USB-CDC protocol parsing/formatting,
# the PiconetBackend, and the platform serial transport. PUBLIC headers
# under include/beebium/econet/{,piconet/} so test code that wants
# direct access to PiconetBackend etc. can link against this and use
# the same #include paths it always did.
add_library(beebium_piconet_core STATIC
    src/Base64.cpp
    src/Discovery.cpp
    src/Events.cpp
    src/PiconetBackend.cpp
    src/Probe.cpp
    ${PICONET_SERIAL_SRCS}
    src/TxResult.cpp
)

target_include_directories(beebium_piconet_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(beebium_piconet_core
    PUBLIC beebium_core
)

set_target_properties(beebium_piconet_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# piconet plugin sources. The plugin DLL contains the
# EconetTransportExtension implementation, the UI, the platform serial
# wiring, and -- when the service layer is built -- a gRPC-free
# PiconetDispatcher plus the piconet_service.proto MESSAGES. It does NOT
# link gRPC.
#
# Why no gRPC: vcpkg builds gRPC as a static library on Windows. When two
# modules in the same process each link gRPC::grpc++ statically, each
# gets its own private copy of gRPC's internal state (thread-local
# ExecCtx, closure lists, iomgr). When the EXE-side gRPC server
# dispatches a server-streaming RPC handler that crosses into the
# plugin DLL (e.g. a gRPC worker thread invokes
# PiconetUi::handle_event), heap corruption manifests as a 32-bit
# zeroing of nearby PiconetBackend members -- specifically the
# serial_factory_ std::function's stored fn-ptr -- yielding an AV at
# the next reopen call. See docs/discussion/grpc-windows-streaming-race.md.
#
# The PiconetService.GetStatus RPC is served through the core's single
# ExtensionRpc channel: PiconetDispatcher (de)serializes the proto
# messages and reads the backend, so the plugin carries protobuf for its
# own messages but never gRPC. See docs/discussion/extension-rpc-channel.md.
set(PICONET_PLUGIN_SOURCES
    PiconetEconetTransportExtension.cpp
    PiconetUi.cpp
    plugin_entry.cpp
)

# piconet_service.proto MESSAGES only (no gRPC stub). The dispatcher
# (de)serializes these; compiling the .grpc.pb.cc is what would pull gRPC
# into the plugin, so it is deliberately omitted.
if(BEEBIUM_BUILD_SERVICE)
    beebium_compile_proto(
        TARGET beebium_ext_piconet
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/piconet_service.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_library(beebium_ext_piconet_proto OBJECT
        ${beebium_ext_piconet_PROTO_SRCS}
    )
    target_include_directories(beebium_ext_piconet_proto
        PUBLIC ${beebium_ext_piconet_OUT_DIR}
    )
    target_link_libraries(beebium_ext_piconet_proto
        PUBLIC protobuf::libprotobuf
    )
    set_target_properties(beebium_ext_piconet_proto PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )

    # Static build of the extension (+ dispatcher) for unit tests that drive
    # PiconetEconetTransportExtension / PiconetDispatcher directly. Linking the
    # proto messages once here (rather than also linking the plugin) keeps a
    # single protobuf descriptor copy in the test process.
    add_library(beebium_ext_piconet STATIC
        PiconetEconetTransportExtension.cpp
        PiconetUi.cpp
    )
    target_include_directories(beebium_ext_piconet
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(beebium_ext_piconet PUBLIC BEEBIUM_BUILD_SERVICE)
    target_link_libraries(beebium_ext_piconet
        PUBLIC
            beebium_piconet_core
            beebium_extension_api
            beebium_ext_piconet_proto
            beebium_extension_ui_proto
    )
    set_target_properties(beebium_ext_piconet PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

# The shared-library plugin: links the protocol core and the
# EconetTransportExtension wrapper, exports beebium_create_extension.
add_library(beebium_ext_piconet_plugin SHARED ${PICONET_PLUGIN_SOURCES})

target_include_directories(beebium_ext_piconet_plugin
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_piconet_plugin
    PRIVATE
        beebium_piconet_core
        beebium_extension_api
)

if(BEEBIUM_BUILD_SERVICE)
    # The dispatcher + proto messages. beebium_extension_api supplies the
    # ExtensionRpcDispatcher ABI; the proto object lib supplies the messages
    # (and pulls protobuf, but not gRPC).
    #
    # PUBLIC (not PRIVATE): the extension's class layout has a member guarded by
    # #ifdef BEEBIUM_BUILD_SERVICE, so every consumer that compiles the header
    # (e.g. the plugin-linking tests) MUST see the same flag, or sizeof differs
    # and an object constructed in one module and destroyed in another corrupts
    # the heap.
    target_compile_definitions(beebium_ext_piconet_plugin PUBLIC BEEBIUM_BUILD_SERVICE)
    target_link_libraries(beebium_ext_piconet_plugin
        PRIVATE beebium_ext_piconet_proto
    )
endif()

set_target_properties(beebium_ext_piconet_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "piconet"
    # On Windows, test binaries (test_grpc_piconet_service, test_piconet_ui)
    # link against this plugin DLL and directly construct
    # PiconetEconetTransportExtension. That class isn't annotated with
    # __declspec(dllexport), so MSVC normally hides it from the import lib.
    # Export all function symbols to give the tests access without having
    # to sprinkle annotations through the plugin sources. No effect on POSIX.
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE
)

beebium_finalize_plugin(TARGET beebium_ext_piconet_plugin NAME piconet)
