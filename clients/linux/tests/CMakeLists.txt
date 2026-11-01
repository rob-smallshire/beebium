# Linux client tests: Catch2, as in the rest of the repository.
#
# Everything here runs headless. Tests that need Qt GUI types run under the
# offscreen platform plugin, set per test so a developer's display is never
# touched: ctest --test-dir <build> --output-on-failure

include(FetchContent)
FetchContent_Declare(
    Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.0.tar.gz
    URL_HASH SHA256=f6d4f8d78a9b59ec72a81d49f58d18eb317372ac07f8d9432710a079e69fd66a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# A Catch2 main that creates a QApplication first, for tests of widgets.
add_library(beebium_linux_qt_test_main STATIC support/QtTestMain.cpp)
target_link_libraries(beebium_linux_qt_test_main PUBLIC Qt6::Widgets Catch2::Catch2)

# beebium_linux_add_test(<name> [QT] SOURCES ... LIBRARIES ...)
#   QT: link the QApplication main instead of Catch2's own.
function(beebium_linux_add_test name)
    cmake_parse_arguments(ARG "QT" "" "SOURCES;LIBRARIES" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    if(ARG_QT)
        target_link_libraries(${name} PRIVATE beebium_linux_qt_test_main)
    else()
        target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain)
    endif()
    target_link_libraries(${name} PRIVATE ${ARG_LIBRARIES})
    target_compile_definitions(${name} PRIVATE
        BEEBIUM_KEYBOARD_CORPUS_DIRPATH="${BEEBIUM_KEYBOARD_CORPUS_DIRPATH}")
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)
    catch_discover_tests(${name}
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

beebium_linux_add_test(test_application_info
    SOURCES test_application_info.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_main_window QT
    SOURCES test_main_window.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_compatibility
    SOURCES test_compatibility.cpp
    LIBRARIES beebium_linux_connection)

beebium_linux_add_test(test_frame_mailbox
    SOURCES test_frame_mailbox.cpp
    LIBRARIES beebium_linux_connection)

beebium_linux_add_test(test_frame_decode
    SOURCES test_frame_decode.cpp
    LIBRARIES beebium_linux_connection)

beebium_linux_add_test(test_picture_geometry
    SOURCES test_picture_geometry.cpp
    LIBRARIES beebium_linux_connection)

beebium_linux_add_test(test_video_stream
    SOURCES test_video_stream.cpp
    LIBRARIES beebium_linux_connection)

beebium_linux_add_test(test_audio_stream QT
    SOURCES test_audio_stream.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_handshake
    SOURCES test_handshake.cpp
    LIBRARIES beebium_linux_connection)

beebium_linux_add_test(test_metrics
    SOURCES test_metrics.cpp
    LIBRARIES beebium_linux_metrics)

beebium_linux_add_test(test_display_uniforms
    SOURCES test_display_uniforms.cpp
    LIBRARIES beebium_linux_display)

beebium_linux_add_test(test_evdev_codes
    SOURCES test_evdev_codes.cpp
    LIBRARIES beebium_linux_keyboard)

beebium_linux_add_test(test_key_resolver
    SOURCES test_key_resolver.cpp
    LIBRARIES beebium_linux_keyboard)

beebium_linux_add_test(test_held_keys
    SOURCES test_held_keys.cpp
    LIBRARIES beebium_linux_keyboard)

beebium_linux_add_test(test_key_sender
    SOURCES test_key_sender.cpp
    LIBRARIES beebium_linux_connection)

beebium_linux_add_test(test_keyboard_input QT
    SOURCES test_keyboard_input.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_mapping_file
    SOURCES test_mapping_file.cpp
    LIBRARIES beebium_linux_keyboard)

beebium_linux_add_test(test_mapping_corpus
    SOURCES test_mapping_corpus.cpp
    LIBRARIES beebium_linux_keyboard)

beebium_linux_add_test(test_caps_lock_sync QT
    SOURCES test_caps_lock_sync.cpp
    LIBRARIES beebium_linux_app)
if(XKBCOMMON_FOUND)
    target_link_libraries(test_caps_lock_sync PRIVATE PkgConfig::XKBCOMMON)
endif()

beebium_linux_add_test(test_presentation QT
    SOURCES test_presentation.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_immersive_window QT
    SOURCES test_immersive_window.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_audio
    SOURCES test_audio.cpp
    LIBRARIES beebium_linux_audio)

beebium_linux_add_test(test_sidebar QT
    SOURCES test_sidebar.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_indicators QT
    SOURCES test_indicators.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_storage QT
    SOURCES test_storage.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_typing QT
    SOURCES test_typing.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_keyboard_mode QT
    SOURCES test_keyboard_mode.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_graphics_api
    SOURCES test_graphics_api.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_liveness
    SOURCES test_liveness.cpp
    LIBRARIES beebium_linux_connection)

beebium_linux_add_test(test_machine_session QT
    SOURCES test_machine_session.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_error_presentation QT
    SOURCES test_error_presentation.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_reconnect_policy
    SOURCES test_reconnect_policy.cpp
    LIBRARIES beebium_linux_connection)

beebium_linux_add_test(test_connection_recovery QT
    SOURCES test_connection_recovery.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_endpoint
    SOURCES test_endpoint.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_client_settings QT
    SOURCES test_client_settings.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_discovered_machines QT
    SOURCES test_discovered_machines.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_home_window QT
    SOURCES test_home_window.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_connecting QT
    SOURCES test_connecting.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_servers QT
    SOURCES test_servers.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_machine_control QT
    SOURCES test_machine_control.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_new_machine QT
    SOURCES test_new_machine.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_close_decision QT
    SOURCES test_close_decision.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_keyboard_focus QT
    SOURCES test_keyboard_focus.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_launch_requests QT
    SOURCES test_launch_requests.cpp
    LIBRARIES beebium_linux_app)

beebium_linux_add_test(test_close_sequence QT
    SOURCES test_close_sequence.cpp
    LIBRARIES beebium_linux_app)
