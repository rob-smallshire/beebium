# Beebium Linux client
#
# A Qt 6 Widgets front-end for Beebium servers, Raspberry Pi OS first. Built
# as a project of its own, as the macOS client is, so it can be built and
# packaged without the emulator core:
#
#   cmake -S clients/linux -B clients/linux/build -G Ninja
#   cmake --build clients/linux/build
#   ctest --test-dir clients/linux/build --output-on-failure
#
# The repository's protos are compiled here at build time with the same
# recipe the servers use (cmake/BeebiumGrpc.cmake, cmake/BeebiumProto.cmake);
# generated code never enters the tree. See
# docs/discussion/linux-client-architecture.md.

cmake_minimum_required(VERSION 3.21)

project(beebium-linux-client
    VERSION 0.15.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(BEEBIUM_LINUX_CLIENT_BUILD_TESTS "Build the Linux client's tests" ON)

get_filename_component(BEEBIUM_REPO_DIRPATH "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# gRPC, Protobuf, OpenSSL and the code generators, found as the servers find them.
include(${BEEBIUM_REPO_DIRPATH}/cmake/BeebiumGrpc.cmake)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra)
endif()

# Shared with the servers, by path.
add_subdirectory(${BEEBIUM_REPO_DIRPATH}/src/host-identity
                 ${CMAKE_CURRENT_BINARY_DIR}/shared/host-identity)

# --- Protocol stubs ---------------------------------------------------------

set(BEEBIUM_SERVICE_PROTO_DIRPATH ${BEEBIUM_REPO_DIRPATH}/src/service/proto)
set(BEEBIUM_EXTENSION_PROTO_DIRPATH ${BEEBIUM_REPO_DIRPATH}/src/core/extension-api/proto)
set(BEEBIUM_PROTO_OUT_DIRPATH ${CMAKE_CURRENT_BINARY_DIR}/generated)

file(GLOB BEEBIUM_SERVICE_PROTO_FILEPATHS CONFIGURE_DEPENDS
     ${BEEBIUM_SERVICE_PROTO_DIRPATH}/*.proto)

beebium_compile_proto(
    TARGET service_protos
    PROTO_FILES ${BEEBIUM_SERVICE_PROTO_FILEPATHS}
    PROTO_PATH ${BEEBIUM_SERVICE_PROTO_DIRPATH}
    OUTPUT_DIR ${BEEBIUM_PROTO_OUT_DIRPATH}
)
beebium_compile_proto(
    TARGET extension_protos
    PROTO_FILES ${BEEBIUM_EXTENSION_PROTO_DIRPATH}/extension_ui.proto
    PROTO_PATH ${BEEBIUM_EXTENSION_PROTO_DIRPATH}
    OUTPUT_DIR ${BEEBIUM_PROTO_OUT_DIRPATH}
)

# The compiled-in protocol fingerprint is the servers' own constant, copied
# into the build tree so the client sees that one header and no other part
# of the service layer. scripts/sync_protocol_fingerprint.py keeps it current.
configure_file(
    ${BEEBIUM_REPO_DIRPATH}/src/service/include/beebium/service/ProtocolFingerprint.hpp
    ${BEEBIUM_PROTO_OUT_DIRPATH}/beebium/service/ProtocolFingerprint.hpp
    COPYONLY
)

add_library(beebium_linux_protos STATIC
    ${service_protos_PROTO_SRCS}
    ${service_protos_GRPC_SRCS}
    ${extension_protos_PROTO_SRCS}
    ${extension_protos_GRPC_SRCS}
)
target_include_directories(beebium_linux_protos PUBLIC ${BEEBIUM_PROTO_OUT_DIRPATH})
target_link_libraries(beebium_linux_protos PUBLIC gRPC::grpc++ protobuf::libprotobuf)

# --- Connection: everything between the wire and the UI, without Qt ---------

add_library(beebium_linux_connection STATIC
    src/connection/Compatibility.cpp
    src/connection/DecodedFrame.cpp
    src/connection/PictureGeometry.cpp
    src/connection/VideoStream.cpp
)
target_include_directories(beebium_linux_connection PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
find_package(Threads REQUIRED)
target_link_libraries(beebium_linux_connection
    PUBLIC
        beebium_linux_protos
        beebium_host_identity
        Threads::Threads
)

# --- The application --------------------------------------------------------

add_library(beebium_linux_app STATIC
    src/app/ApplicationInfo.cpp
    src/app/MainWindow.cpp
)
target_include_directories(beebium_linux_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(beebium_linux_app
    PRIVATE BEEBIUM_LINUX_CLIENT_VERSION="${PROJECT_VERSION}")
target_link_libraries(beebium_linux_app
    PUBLIC
        Qt6::Widgets
        beebium_linux_connection
)

qt_add_executable(beebium src/app/main.cpp)
target_link_libraries(beebium PRIVATE beebium_linux_app)

# --- Tests ------------------------------------------------------------------

if(BEEBIUM_LINUX_CLIENT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
