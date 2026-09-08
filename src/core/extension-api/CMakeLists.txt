# Extension API shared library
#
# The formal ABI surface that extension plugins link against. Contains the
# extension framework types (PeripheralExtension, ExtensionContext, ports)
# and the plugin loader. Plugins resolve these symbols from this shared
# library at load time, ensuring exactly one copy exists at runtime.
#
# The bulk of beebium_core (6502, VIA, CRTC, video, audio, disc, etc.)
# remains static in the main executable and is NOT part of this library.

# Generate extension_ui.pb.{cc,h} and extension_ui.grpc.pb.{cc,h} from the
# proto file owned by the extension framework. The schema is part of the
# extension ABI (extensions produce View trees the framework streams to
# clients), so it lives next to the rest of the extension API.
#
# These compile into a *separate* shared library (beebium_extension_ui_proto)
# rather than into beebium_extension_api itself, because beebium_extension_api
# uses CXX_VISIBILITY_PRESET=hidden to control its ABI surface -- which
# would hide the proto symbols from external consumers (plugins, the
# server's ExtensionUiServiceImpl). The dedicated proto library keeps
# default visibility so the proto types are reachable from any consumer
# while still living in exactly one .so in the process.
set(EXT_UI_PROTO_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${EXT_UI_PROTO_OUT_DIR})

set(EXT_UI_PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/extension_ui.proto)
# Split protoc output into the *message* sources (.pb.cc / .pb.h)
# and the *gRPC service stub* sources (.grpc.pb.cc / .grpc.pb.h).
# Plugin DLLs only need messages; gRPC stubs go into a static library
# that's linked into the server EXE only. See the rationale at the
# beebium_extension_ui_proto target below.
set(EXT_UI_PROTO_MSG_SRCS  ${EXT_UI_PROTO_OUT_DIR}/extension_ui.pb.cc)
set(EXT_UI_PROTO_GRPC_SRCS ${EXT_UI_PROTO_OUT_DIR}/extension_ui.grpc.pb.cc)
set(EXT_UI_PROTO_SRCS
    ${EXT_UI_PROTO_MSG_SRCS}
    ${EXT_UI_PROTO_GRPC_SRCS}
)
set(EXT_UI_PROTO_HDRS
    ${EXT_UI_PROTO_OUT_DIR}/extension_ui.pb.h
    ${EXT_UI_PROTO_OUT_DIR}/extension_ui.grpc.pb.h
)

# protoc's dllexport_decl option stamps every generated message class
# and its per-message static globals (default-instance, ClassDataFull,
# descriptor-pool getter) with BEEBIUM_EXT_UI_PROTO_API. The macro is
# defined by beebium/extension_ui_proto_export.h, force-included into
# every translation unit that parses the generated headers via the
# beebium_ext_ui_proto_export INTERFACE target below. Without
# dllexport_decl on --cpp_out, the data globals are not exported from
# beebium_extension_ui_proto.dll on Windows and consumers (server exes,
# piconet plugin, test binaries) hit LNK2019 on the
# _<Type>_default_instance_ symbols.
#
# grpc_cpp_plugin in the vcpkg / Homebrew versions we build against
# does not accept dllexport_decl, so gRPC service-stub symbols go
# through WINDOWS_EXPORT_ALL_SYMBOLS on the library target below.
add_custom_command(
    OUTPUT ${EXT_UI_PROTO_SRCS} ${EXT_UI_PROTO_HDRS}
    COMMAND ${PROTOC_EXECUTABLE}
        --cpp_out=dllexport_decl=BEEBIUM_EXT_UI_PROTO_API:${EXT_UI_PROTO_OUT_DIR}
        --grpc_out=${EXT_UI_PROTO_OUT_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
        -I${CMAKE_CURRENT_SOURCE_DIR}/proto
        ${EXT_UI_PROTO_FILE}
    DEPENDS ${EXT_UI_PROTO_FILE}
    COMMENT "Generating gRPC/Protobuf sources for extension_ui.proto"
)

# INTERFACE target: carries the force-include option and include
# directory for extension_ui_proto_export.h. beebium_extension_ui_proto
# PUBLIC-links it so every transitive consumer sees /FI (MSVC) or
# -include (GCC/Clang) on its compile line and the BEEBIUM_EXT_UI_PROTO_API
# macro is defined before the generated headers are parsed.
add_library(beebium_ext_ui_proto_export INTERFACE)
target_include_directories(beebium_ext_ui_proto_export INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(beebium_ext_ui_proto_export INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/FI beebium/extension_ui_proto_export.h>
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-include beebium/extension_ui_proto_export.h>
)

# Unconditional SHARED: one copy of proto descriptors and class
# definitions per process. Without this, a dlopen'd / LoadLibrary'd
# plugin (Piconet) that also consumes these types would register
# duplicate descriptors into protobuf's global pool and abort on init
# (seen on Windows with STATIC when piconet.dll loads alongside the
# test binary's embedded copy).
#
# History: this library was STATIC on Windows while Piconet was
# POSIX-only; see docs/discussion/win32-piconet-gap.md.
# beebium_extension_ui_proto contains *only* the protoc-generated
# message types (.pb.cc), not the gRPC service stubs (.grpc.pb.cc).
# This is deliberate and load-bearing on Windows: vcpkg builds gRPC
# as a static library, so any DLL that links gRPC::grpc++ ends up
# with its own private copy of gRPC's internal state (thread-local
# ExecCtx, closure lists, iomgr). Cross-DLL gRPC operations race on
# mismatched per-module state and crash with an access violation in
# grpc_closure_list_append. See docs/discussion/grpc-windows-streaming-race.md
# for the diagnosis with stack trace.
#
# Plugins that #include extension_ui.pb.h get the message types they
# need (View, Control, Label, Indicator, Toggle, Button, Choice,
# TextInput, Group, ModalEditor, EditableChoice, EditorCommit, ...)
# without pulling in any gRPC code. The gRPC service stubs and the
# ExtensionUiServiceImpl handler live in the static
# beebium_extension_ui_service library below, which is linked only
# into the server EXE.
add_library(beebium_extension_ui_proto SHARED
    ${EXT_UI_PROTO_MSG_SRCS}
)

target_include_directories(beebium_extension_ui_proto PUBLIC
    ${EXT_UI_PROTO_OUT_DIR}
)

target_link_libraries(beebium_extension_ui_proto
    PUBLIC
        beebium_ext_ui_proto_export
        protobuf::libprotobuf
)

set_target_properties(beebium_extension_ui_proto PROPERTIES
    MACOSX_RPATH ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    POSITION_INDEPENDENT_CODE ON
    # Covers the gRPC service-stub function symbols (grpc_cpp_plugin
    # does not accept dllexport_decl). No effect on POSIX.
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE
)

# On Windows, place the DLL next to the server executables so the
# loader finds it without manual PATH setup, and copy it into the
# tests directory so catch_discover_tests can launch each test binary
# at configure time. Mirrors the beebium_extension_api.dll arrangement.
if(WIN32)
    set_target_properties(beebium_extension_ui_proto PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/server"
    )
    add_custom_command(TARGET beebium_extension_ui_proto POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/tests/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:beebium_extension_ui_proto>
            "${CMAKE_BINARY_DIR}/tests/$<CONFIG>/"
        COMMENT "Copying beebium_extension_ui_proto.dll to tests/$<CONFIG> for runtime loading"
    )
endif()

# beebium_extension_ui_service: gRPC service stubs for ExtensionUiService
# plus the handler implementation. Static so it links into exactly one
# module (the server EXE / test EXE), keeping gRPC's per-module state
# (thread-local ExecCtx, closure lists, iomgr) confined to that module.
# Linking gRPC into multiple DLLs in the same process race-corrupts the
# closure list -- see the rationale on beebium_extension_ui_proto above
# and the diagnosis in docs/discussion/grpc-windows-streaming-race.md.
add_library(beebium_extension_ui_service STATIC
    ${EXT_UI_PROTO_GRPC_SRCS}
    ExtensionUiService.cpp
)

target_include_directories(beebium_extension_ui_service PUBLIC
    ${EXT_UI_PROTO_OUT_DIR}
)
target_include_directories(beebium_extension_ui_service PRIVATE
    # ExtensionUiServiceImpl pulls in extension/Extension.hpp,
    # ExtensionUi.hpp, ExtensionRegistry.hpp, EconetTransportRegistry.hpp.
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    # And the service-layer header that declares the class itself.
    ${CMAKE_SOURCE_DIR}/src/service/include
)

target_link_libraries(beebium_extension_ui_service
    PUBLIC
        beebium_ext_ui_proto_export
        beebium_extension_ui_proto
        gRPC::grpc++
)

set_target_properties(beebium_extension_ui_service PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Suppress warnings in generated proto/gRPC code.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set_source_files_properties(
        ${EXT_UI_PROTO_SRCS}
        PROPERTIES
        COMPILE_FLAGS "-Wno-unused-parameter -Wno-shadow"
    )
elseif(MSVC)
    set_source_files_properties(
        ${EXT_UI_PROTO_SRCS}
        PROPERTIES
        COMPILE_FLAGS "/wd4100 /wd4127 /wd4244 /wd4267"
    )
endif()

add_library(beebium_extension_api SHARED
    Extension.cpp
    ExtensionContext.cpp
    IndicatorFilter.cpp
    OneMHzBusPort.cpp
    EconetTransportExtension.cpp
    CpuDebugTarget.cpp
    CoprocessorExtension.cpp
    ExtensionStorage.cpp
    ExtensionUi.cpp
    ExtensionRpc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/UserPort.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/PluginLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ExtensionArgParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ExtensionResolver.cpp
)

target_include_directories(beebium_extension_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    # third_party (moodycamel) is part of the extension-facing surface
    # because Indicators.hpp -- now reachable through ExtensionContext --
    # includes <moodycamel/readerwriterqueue.h>.
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party
)

# nlohmann_json is needed by PluginLoader for manifest parsing.
# beebium_extension_ui_proto is PUBLIC so plugins (and the server) get
# the View / DispatchRequest headers transitively when they link
# beebium_extension_api.
target_link_libraries(beebium_extension_api
    PUBLIC
        nlohmann_json::nlohmann_json
        beebium_extension_ui_proto
    PRIVATE ${CMAKE_DL_LIBS}
)

set_target_properties(beebium_extension_api PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    MACOSX_RPATH ON
    # On Linux/macOS, place the shared object in build/lib/. The server
    # executables have an RPATH (or @rpath on macOS) pointing here.
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Windows has no RPATH mechanism: DLLs are searched only in the directory
# of the loading executable (and on PATH/system dirs). Override
# RUNTIME_OUTPUT_DIRECTORY to place beebium_extension_api.dll directly
# alongside the server executables so they can load it without manual
# PATH setup or a deployment copy step.
#
# Test executables live in build/tests/$<CONFIG>/ and also link
# beebium_core (which PUBLIC-links beebium_extension_api), so they
# need a sibling copy of the DLL to load at runtime. A post-build
# custom command copies the DLL into the tests directory so ctest
# (including catch_discover_tests, which invokes each test exe to
# enumerate cases) can load them. This matches the existing
# copy-alongside-server arrangement above for deployment targets.
if(WIN32)
    set_target_properties(beebium_extension_api PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/server"
    )
    add_custom_command(TARGET beebium_extension_api POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/tests/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:beebium_extension_api>
            "${CMAKE_BINARY_DIR}/tests/$<CONFIG>/"
        COMMENT "Copying beebium_extension_api.dll to tests/$<CONFIG> for runtime loading"
    )
endif()

# Install the extension ABI shared libraries into <prefix>/lib. The server
# executables carry an INSTALL_RPATH of $ORIGIN/../lib (@loader_path/../lib on
# macOS) so they resolve these from the installed tree. Without these rules an
# installed server cannot start: both libraries are direct runtime dependencies
# of every server executable. (On POSIX these are LIBRARY artifacts; the Windows
# RUNTIME case is handled by its own packaging path and is not covered here.)
install(TARGETS beebium_extension_api beebium_extension_ui_proto
    LIBRARY DESTINATION lib
)
