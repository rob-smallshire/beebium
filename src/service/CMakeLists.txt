# beebium_service - gRPC service layer

# Proto files
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/video.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/keyboard.proto
)

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
set(SERVICE_SOURCES
    src/Server.cpp
    src/VideoService.cpp
    src/KeyboardService.cpp
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
endif()
