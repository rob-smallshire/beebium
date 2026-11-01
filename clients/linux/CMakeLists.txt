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

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets ShaderTools)

# nlohmann-json for keyboard mapping files, found as the root build finds it:
# an installed copy, else the same pinned release fetched.
find_package(nlohmann_json 3.11 CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
        URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# The keyboard mapping conformance corpus. Its built-ins are also the
# built-ins the client ships (docs/keyboard-mapping-files.md 5, 6): one copy.
set(BEEBIUM_KEYBOARD_CORPUS_DIRPATH ${BEEBIUM_REPO_DIRPATH}/tests/assets/keyboard-mappings)
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

# --- Metrics: frame pacing and CPU use, without Qt -------------------------

add_library(beebium_linux_metrics STATIC
    src/metrics/IntervalStats.cpp
    src/metrics/ThreadCpu.cpp
)
target_include_directories(beebium_linux_metrics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- Keyboard: host keys to BBC keys and the wire events, pure --------------

add_library(beebium_linux_keyboard STATIC
    src/keyboard/BbcKeyTable.cpp
    src/keyboard/EvdevCodes.cpp
    src/keyboard/HeldKeys.cpp
    src/keyboard/KeyResolver.cpp
    src/keyboard/MappingFile.cpp
)
target_include_directories(beebium_linux_keyboard PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Only BbcKeyTable::from_proto reads a proto message.
target_link_libraries(beebium_linux_keyboard
    PUBLIC
        beebium_linux_protos
        nlohmann_json::nlohmann_json
)

# --- Connection: everything between the wire and the UI, without Qt ---------

add_library(beebium_linux_connection STATIC
    src/connection/Compatibility.cpp
    src/connection/DecodedFrame.cpp
    src/connection/GuestReadyWatcher.cpp
    src/connection/Handshake.cpp
    src/connection/KeySender.cpp
    src/connection/PictureGeometry.cpp
    src/connection/VideoStream.cpp
)
target_include_directories(beebium_linux_connection PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
find_package(Threads REQUIRED)
target_link_libraries(beebium_linux_connection
    PUBLIC
        beebium_linux_protos
        beebium_linux_keyboard
        beebium_linux_metrics
        beebium_host_identity
        Threads::Threads
)

# --- Display: the QRhi renderer, without widgets ----------------------------

add_library(beebium_linux_display STATIC
    src/display/DisplayRenderer.cpp
    src/display/DisplayUniforms.cpp
)
target_include_directories(beebium_linux_display PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# QRhi (rhi/qrhi.h) is semi-public API with limited compatibility promises.
# Debian packages its headers with the private ones (qt6-base-private-dev,
# under a versioned include path), so they are reached through GuiPrivate.
target_link_libraries(beebium_linux_display
    PUBLIC
        Qt6::Gui
        Qt6::GuiPrivate
        beebium_linux_connection
)
# Baked at build time: SPIR-V for Vulkan, GLSL ES 3.00 for OpenGL ES, GLSL
# 1.40 and 1.50 for desktop GL 3.1 and 3.2+ (Mesa's desktop GL on the Pi's
# V3D is 3.1), and the default HLSL and MSL, so nothing is compiled at run
# time.
qt_add_shaders(beebium_linux_display "display_shaders"
    PREFIX "/"
    GLSL "300es,140,150"
    FILES
        shaders/display.vert
        shaders/display.frag
)

# --- The application --------------------------------------------------------

add_library(beebium_linux_app STATIC
    src/app/ApplicationInfo.cpp
    src/app/DisplayWidget.cpp
    src/app/GraphicsApi.cpp
    src/app/HostLockState.cpp
    src/app/KeyboardInput.cpp
    src/app/MachineSession.cpp
    src/app/MainWindow.cpp
    src/app/SpikeMetrics.cpp
)
target_include_directories(beebium_linux_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(beebium_linux_app
    PRIVATE BEEBIUM_LINUX_CLIENT_VERSION="${PROJECT_VERSION}")
target_link_libraries(beebium_linux_app
    PUBLIC
        Qt6::Widgets
        beebium_linux_display
)

# Reading the host's Caps Lock (src/app/HostLockState.cpp): xcb on X11,
# wayland-client and xkbcommon on Wayland. Without them the client still
# builds, and Caps Lock synchronisation does nothing.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(XCB IMPORTED_TARGET xcb)
    pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)
    pkg_check_modules(XKBCOMMON IMPORTED_TARGET xkbcommon)
endif()
if(XCB_FOUND)
    target_compile_definitions(beebium_linux_app PRIVATE BEEBIUM_HAS_X11_LOCKS)
    target_link_libraries(beebium_linux_app PRIVATE PkgConfig::XCB)
endif()
if(WAYLAND_CLIENT_FOUND AND XKBCOMMON_FOUND)
    target_compile_definitions(beebium_linux_app PRIVATE BEEBIUM_HAS_WAYLAND_LOCKS)
    target_link_libraries(beebium_linux_app PRIVATE PkgConfig::WAYLAND_CLIENT PkgConfig::XKBCOMMON)
endif()

qt_add_resources(beebium_linux_app "keyboard_mappings"
    PREFIX "/keyboard-mappings"
    BASE ${BEEBIUM_KEYBOARD_CORPUS_DIRPATH}
    FILES
        ${BEEBIUM_KEYBOARD_CORPUS_DIRPATH}/Default.json
        ${BEEBIUM_KEYBOARD_CORPUS_DIRPATH}/Logical.json
        ${BEEBIUM_KEYBOARD_CORPUS_DIRPATH}/Arcadians.json
        ${BEEBIUM_KEYBOARD_CORPUS_DIRPATH}/Boffin.json
        ${BEEBIUM_KEYBOARD_CORPUS_DIRPATH}/Planetoid.json
        ${BEEBIUM_KEYBOARD_CORPUS_DIRPATH}/Snapper.json
        ${BEEBIUM_KEYBOARD_CORPUS_DIRPATH}/Thrust.json
)

qt_add_executable(beebium src/app/main.cpp)
target_link_libraries(beebium PRIVATE beebium_linux_app)

# The renderer against a live server, offscreen: frame cost and CPU use on a
# machine with no display. See src/bench/DisplayBench.cpp.
qt_add_executable(beebium-display-bench src/bench/DisplayBench.cpp)
target_link_libraries(beebium-display-bench PRIVATE beebium_linux_app)

# --- Tests ------------------------------------------------------------------

if(BEEBIUM_LINUX_CLIENT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
