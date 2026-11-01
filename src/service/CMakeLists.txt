# beebium_service - gRPC service layer

# Proto files
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/video.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/keyboard.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/debugger.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/disc.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/indicator.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/system.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/audio.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/sideways.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/econet.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/econet_transport.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/serial.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/tube.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/peripheral_extension.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/extension_rpc.proto
)
# Note: extension_ui.proto lives under src/core/extension-api/proto/ and
# is compiled into beebium_extension_api so plugins (and the server's
# ExtensionUiServiceImpl) all see the same proto descriptors via the
# shared library. See src/core/extension-api/CMakeLists.txt.

# Output directories for generated code
set(PROTO_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_OUT_DIR})

# Generate C++ sources from proto files
set(PROTO_SRCS)
set(PROTO_HDRS)
set(GRPC_SRCS)
set(GRPC_HDRS)

foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)

    list(APPEND PROTO_SRCS ${PROTO_OUT_DIR}/${PROTO_NAME}.pb.cc)
    list(APPEND PROTO_HDRS ${PROTO_OUT_DIR}/${PROTO_NAME}.pb.h)
    list(APPEND GRPC_SRCS ${PROTO_OUT_DIR}/${PROTO_NAME}.grpc.pb.cc)
    list(APPEND GRPC_HDRS ${PROTO_OUT_DIR}/${PROTO_NAME}.grpc.pb.h)

    add_custom_command(
        OUTPUT
            ${PROTO_OUT_DIR}/${PROTO_NAME}.pb.cc
            ${PROTO_OUT_DIR}/${PROTO_NAME}.pb.h
            ${PROTO_OUT_DIR}/${PROTO_NAME}.grpc.pb.cc
            ${PROTO_OUT_DIR}/${PROTO_NAME}.grpc.pb.h
        COMMAND ${PROTOC_EXECUTABLE}
            --cpp_out=${PROTO_OUT_DIR}
            --grpc_out=${PROTO_OUT_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
            -I${CMAKE_CURRENT_SOURCE_DIR}/proto
            ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Generating gRPC/Protobuf sources for ${PROTO_NAME}.proto"
    )
endforeach()

# Service implementation sources
# Note: Server.hpp and DebuggerService.hpp are now header-only templates
set(SERVICE_SOURCES
    src/VideoService.cpp
    src/KeyboardService.cpp
    src/ConnectionTracker.cpp
    src/ShutdownCoordinator.cpp
    src/ExtensionRpcService.cpp
)

# Create the library
add_library(beebium_service STATIC
    ${SERVICE_SOURCES}
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

target_include_directories(beebium_service
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PROTO_OUT_DIR}
)

target_link_libraries(beebium_service
    PUBLIC
        beebium_core
        beebium_discovery
        # ExtensionUiServiceImpl is constructed in ServerMain.hpp (server
        # EXE) and in test fixtures. Static so it links into a single
        # module per process, keeping gRPC's per-module thread-local
        # state (ExecCtx, closure lists) confined there. See
        # docs/discussion/grpc-windows-streaming-race.md.
        beebium_extension_ui_service
        # ExtensionRpcServiceImpl bridges the gRPC-free dispatcher ABI
        # (RpcContext/RpcResponseWriter/ExtensionRpcDispatcher, anchored in
        # beebium_extension_api) to the ExtensionRpc gRPC service.
        beebium_extension_api
        beebium_host_identity
        gRPC::grpc++
        protobuf::libprotobuf
)

# Suppress warnings in generated code
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set_source_files_properties(
        ${PROTO_SRCS}
        ${GRPC_SRCS}
        PROPERTIES
        COMPILE_FLAGS "-Wno-unused-parameter -Wno-shadow"
    )
elseif(MSVC)
    # Suppress warnings in generated protobuf/gRPC code:
    # C4100: unreferenced formal parameter
    # C4127: conditional expression is constant
    # C4244: conversion from 'type1' to 'type2', possible loss of data
    # C4267: conversion from 'size_t' to 'type', possible loss of data
    set_source_files_properties(
        ${PROTO_SRCS}
        ${GRPC_SRCS}
        PROPERTIES
        COMPILE_FLAGS "/wd4100 /wd4127 /wd4244 /wd4267"
    )
    # The per-source flags above only reach the generated .cc files. The
    # generated *.pb.h headers narrow size_t in their inline setters, so every
    # TU that includes them -- this library, the server executables, and the
    # tests -- trips C4244/C4267. Propagate the suppression to those consumers.
    target_compile_options(beebium_service PUBLIC /wd4244 /wd4267)
endif()
