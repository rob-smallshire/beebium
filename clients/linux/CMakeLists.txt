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
    VERSION 0.16.1
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

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets DBus Network ShaderTools)
# QRhi's headers come through Qt6::GuiPrivate (see the display library). Qt 6.9
# and later declare private modules only when asked for by name; earlier Qt
# declares them with the public module.
if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()
# Sound output (client specification 8): optional, so the rest builds and
# is tested where Qt Multimedia is not installed; without it the client
# says it has no sound output.
find_package(Qt6 6.7 QUIET COMPONENTS Multimedia)
if(TARGET Qt6::Multimedia)
    message(STATUS "Beebium Linux client: sound output through Qt Multimedia")
else()
    message(STATUS "Beebium Linux client: no Qt Multimedia, so no sound output")
endif()

# No signals/slots/emit keywords: the generated protobuf code has accessors
# named signals(), which the keyword macro would break. Q_SIGNALS, Q_SLOTS
# and Q_EMIT instead.
add_compile_definitions(QT_NO_KEYWORDS)

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
set(BEEBIUM_PRESET_FIXTURES_DIRPATH ${BEEBIUM_REPO_DIRPATH}/tests/assets/presets)
set(BEEBIUM_PRESET_SCHEMA_FIXTURES_DIRPATH ${BEEBIUM_REPO_DIRPATH}/tests/assets/describe_preset_schema)
qt_standard_project_setup()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra)
endif()

# Shared with the servers, by path.
add_subdirectory(${BEEBIUM_REPO_DIRPATH}/src/host-identity
                 ${CMAKE_CURRENT_BINARY_DIR}/shared/host-identity)
# mDNS browsing (Avahi on Linux, loaded at run time) for the Connect dialog.
add_subdirectory(${BEEBIUM_REPO_DIRPATH}/src/discovery
                 ${CMAKE_CURRENT_BINARY_DIR}/shared/discovery)

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

# --- Presets: the machine draft and its resolution, without Qt -------------

add_library(beebium_linux_preset STATIC
    src/preset/MachineDraft.cpp
    src/preset/ParameterFormModel.cpp
    src/preset/PresetDocument.cpp
    src/preset/PresetResolution.cpp
    src/preset/PresetSchema.cpp
    src/preset/SidewaysDraft.cpp
    src/preset/StartupLinks.cpp
    src/preset/StructuredItem.cpp
)
target_include_directories(beebium_linux_preset PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# The forms are Views for the extension views' renderer.
target_link_libraries(beebium_linux_preset PUBLIC nlohmann_json::nlohmann_json beebium_linux_protos)

# --- Connection: everything between the wire and the UI, without Qt ---------

# The audio path without a sink: the format, the ring, the pre-roll and the
# renderer (client specification 8). No Qt.
add_library(beebium_linux_audio STATIC
    src/audio/AudioLayout.cpp
    src/audio/AudioRenderer.cpp
    src/audio/FrameRing.cpp
    src/audio/PreRoll.cpp
)
target_include_directories(beebium_linux_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(beebium_linux_audio PUBLIC beebium_linux_protos)

add_library(beebium_linux_connection STATIC
    src/connection/AudioStream.cpp
    src/connection/DiscStream.cpp
    src/connection/EconetPresentation.cpp
    src/connection/ExtensionDispatch.cpp
    src/connection/IndicatorStream.cpp
    src/connection/Compatibility.cpp
    src/connection/DecodedFrame.cpp
    src/connection/Endpoint.cpp
    src/connection/Handshake.cpp
    src/connection/KeySender.cpp
    src/connection/Liveness.cpp
    src/connection/PeripheralTree.cpp
    src/connection/PictureGeometry.cpp
    src/connection/ReconnectPolicy.cpp
    src/connection/RpcWorker.cpp
    src/connection/SidewaysTopology.cpp
    src/connection/VideoStream.cpp
)
target_include_directories(beebium_linux_connection PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
find_package(Threads REQUIRED)
target_link_libraries(beebium_linux_connection
    PUBLIC
        beebium_linux_protos
        beebium_linux_audio
        beebium_linux_keyboard
        beebium_linux_metrics
        beebium_host_identity
        Threads::Threads
)

# --- Servers: finding, querying and launching this host's servers ----------

add_library(beebium_linux_servers STATIC
    src/servers/CoreCli.cpp
    src/servers/ServerLauncher.cpp
    src/servers/ServerLocator.cpp
)
target_include_directories(beebium_linux_servers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(beebium_linux_servers PUBLIC Qt6::Core)

# --- Display: the QRhi renderer, without widgets ----------------------------

add_library(beebium_linux_display STATIC
    src/display/DisplayRenderer.cpp
    src/display/DisplayUniforms.cpp
    src/display/VideoSettings.cpp
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
    src/app/Application.cpp
    src/app/ApplicationInfo.cpp
    src/app/AudioOutput.cpp
    src/app/CardGrid.cpp
    src/app/ClientSettings.cpp
    src/app/CloseDecision.cpp
    src/app/MachineMove.cpp
    src/app/ConnectionOverlay.cpp
    src/app/ConnectionRecovery.cpp
    src/app/DeepLink.cpp
    src/app/DiscoveredMachines.cpp
    src/app/DisplayFrame.cpp
    src/app/DisplayWidget.cpp
    src/app/ErrorPresentation.cpp
    src/app/ExtensionPanel.cpp
    src/app/ExtensionViewWidget.cpp
    src/app/FileReveal.cpp
    src/app/FilesystemSharing.cpp
    src/app/GraphicsApi.cpp
    src/app/HomeWindow.cpp
    src/app/HostLockState.cpp
    src/app/ImmersiveExit.cpp
    src/app/IndicatorStrip.cpp
    src/app/KeyboardInput.cpp
    src/app/KeyboardSidebar.cpp
    src/app/LedColour.cpp
    src/app/MachineSession.cpp
    src/app/MappingCatalogue.cpp
    src/app/MainWindow.cpp
    src/app/NetworkSidebar.cpp
    src/app/NewMachineDialog.cpp
    src/app/configuration/ConfigurationSections.cpp
    src/app/configuration/SectionWidgets.cpp
    src/app/configuration/MemorySection.cpp
    src/app/configuration/StorageSection.cpp
    src/app/ParameterForm.cpp
    src/app/PeripheralsSidebar.cpp
    src/app/PictureDrop.cpp
    src/app/Presentation.cpp
    src/app/PresetResolver.cpp
    src/app/PresetsPage.cpp
    src/app/RunHold.cpp
    src/app/RunningMachinesPage.cpp
    src/app/SavedMachines.cpp
    src/app/SavedMachinesPage.cpp
    src/app/SessionAudio.cpp
    src/app/SessionDiscs.cpp
    src/app/SessionIndicators.cpp
    src/app/SessionEconet.cpp
    src/app/SessionExtensions.cpp
    src/app/SessionPeripherals.cpp
    src/app/SessionSideways.cpp
    src/app/SessionSpeed.cpp
    src/app/SpeedScale.cpp
    src/app/ProcessorSidebar.cpp
    src/app/VideoSidebar.cpp
    src/app/MemorySidebar.cpp
    src/app/ColourChoice.cpp
    src/app/SessionTyping.cpp
    src/app/SettingsDialog.cpp
    src/app/Shortcuts.cpp
    src/app/WindowGeometry.cpp
    src/app/Sidebar.cpp
    src/app/SidebarMode.cpp
    src/app/SingleInstance.cpp
    src/app/SoundSidebar.cpp
    src/app/SpikeMetrics.cpp
    src/app/StorageSidebar.cpp
    src/app/SystemPowerMonitor.cpp
)
target_include_directories(beebium_linux_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(beebium_linux_app
    PRIVATE BEEBIUM_LINUX_CLIENT_VERSION="${PROJECT_VERSION}")
if(TARGET Qt6::Multimedia)
    target_link_libraries(beebium_linux_app PUBLIC Qt6::Multimedia)
    target_compile_definitions(beebium_linux_app PRIVATE BEEBIUM_HAVE_QT_MULTIMEDIA=1)
else()
    target_compile_definitions(beebium_linux_app PRIVATE BEEBIUM_HAVE_QT_MULTIMEDIA=0)
endif()
target_link_libraries(beebium_linux_app
    PUBLIC
        Qt6::Widgets
        Qt6::DBus
        Qt6::Network
        beebium_linux_display
        beebium_linux_preset
        beebium_linux_servers
        beebium_discovery
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
# A server's pace without the GUI, for diagnosing sound that underruns.
add_executable(beebium-pacing-probe tools/pacing-probe/main.cpp)
target_link_libraries(beebium-pacing-probe PRIVATE beebium_linux_connection)

# Type text into a server's machine, for acceptance runs on a desktop.
add_executable(beebium-type tools/type-text/main.cpp)
target_link_libraries(beebium-type PRIVATE beebium_linux_protos)

qt_add_executable(beebium-display-bench src/bench/DisplayBench.cpp)
target_link_libraries(beebium-display-bench PRIVATE beebium_linux_app)

# --- Tests ------------------------------------------------------------------

if(BEEBIUM_LINUX_CLIENT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Installing and packaging ------------------------------------------------
# A desktop application as freedesktop lays one out, under /usr when packaged:
# the program, its desktop entry, MIME types for its documents, its AppStream
# metainfo and its icons (packaging/). Only the program is installed of the
# executables built here.
include(GNUInstallDirs)
install(TARGETS beebium RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/org.beebium.Beebium.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/org.beebium.Beebium.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
install(FILES packaging/org.beebium.Beebium.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(DIRECTORY packaging/icons/hicolor
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)

# The .deb, built on the distribution it is for (Debian trixie, and so
# Raspberry Pi OS trixie): unlike the self-contained server it links the
# distribution's Qt 6 and gRPC, so dpkg-shlibdeps derives those Depends from
# the program, never a hand list. What shlibdeps cannot see, Qt loads at run
# time: the Wayland platform plugin and the Vulkan loader. The desktop, MIME
# and icon caches are refreshed by the distribution's dpkg triggers, so there
# are no maintainer scripts. The servers are found, not depended on (settings,
# PATH, /opt/beebium/bin), so the server package is only recommended.
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "beebium-client")
set(CPACK_PACKAGE_VENDOR "Robert Smallshire")
# The distribution the .deb is built for, as a tilde suffix on its version
# (0.16.0~debian13): a package built for one distribution's libraries is a
# different package from one built for another's, and the suffix says which.
# The tilde sorts it below a plain 0.16.0. CI sets it per distribution; a local
# build leaves it empty and gets the plain version.
set(BEEBIUM_CLIENT_PACKAGE_DISTRIBUTION "" CACHE STRING
    "Distribution suffix for the .deb version, e.g. debian13 or ubuntu26.04; empty for none")
if(BEEBIUM_CLIENT_PACKAGE_DISTRIBUTION)
    if(NOT BEEBIUM_CLIENT_PACKAGE_DISTRIBUTION MATCHES "^[a-z][a-z0-9.]*$")
        message(FATAL_ERROR
            "BEEBIUM_CLIENT_PACKAGE_DISTRIBUTION '${BEEBIUM_CLIENT_PACKAGE_DISTRIBUTION}' "
            "must be lower-case letters, digits and dots, starting with a letter")
    endif()
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}~${BEEBIUM_CLIENT_PACKAGE_DISTRIBUTION}")
else()
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
endif()
set(CPACK_PACKAGE_CONTACT "Robert Smallshire <robert@smallshire.org.uk>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Beebium BBC Micro emulator, the desktop application")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
    "Beebium BBC Micro emulator, the desktop application\n"
    " The window onto Beebium's emulator servers: start a BBC Micro from a\n"
    " preset, open a saved machine, or connect to one running on the network.\n"
    " The servers come in the beebium-server package.")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Robert Smallshire <robert@smallshire.org.uk>")
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/rob-smallshire/beebium")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-wayland, libvulkan1")
# The client sets no style, palette or font of its own: it takes them from
# Qt's platform theme. Where a desktop sets none up for Qt 6 (plain Debian
# trixie under labwc or another wlroots compositor), the client falls to Qt's
# generic theme, Fusion with a light palette, whatever the GTK theme; with the
# GTK platform theme it follows the GTK theme. Raspberry Pi OS's own session
# sets qt6ct through QT_QPA_PLATFORMTHEME, which already carries its palette and
# font, so there the GTK theme is used only when that variable says gtk3.
# Recommended, not depended on: the client runs without it.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
    "beebium-server, avahi-daemon, libavahi-client3, qt6-gtk-platformtheme")
# With a distribution, the file name puts a dot where the version has its
# tilde (beebium-client_0.16.0.debian13_amd64.deb): GitHub rewrites "~" to "."
# in release asset names, so the file is named as it will be published.
if(BEEBIUM_CLIENT_PACKAGE_DISTRIBUTION)
    execute_process(COMMAND dpkg --print-architecture
        OUTPUT_VARIABLE beebium_client_deb_arch
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    set(CPACK_DEBIAN_FILE_NAME
        "beebium-client_${PROJECT_VERSION}.${BEEBIUM_CLIENT_PACKAGE_DISTRIBUTION}_${beebium_client_deb_arch}.deb")
else()
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
endif()
# Debug information stays with the build, not the package.
set(CPACK_STRIP_FILES ON)
include(CPack)
