# Beebium tests

# Test assets directory
set(BEEBIUM_TEST_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")

# 6502 tests
add_executable(test_6502 test_6502.cpp)
target_link_libraries(test_6502 PRIVATE 6502_lib Catch2::Catch2WithMain)
target_compile_definitions(test_6502 PRIVATE
    BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
)
catch_discover_tests(test_6502)

# Memory tests
add_executable(test_memory test_memory.cpp)
target_link_libraries(test_memory PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_memory)

# Memory histogram tests
add_executable(test_memory_histogram test_memory_histogram.cpp)
target_link_libraries(test_memory_histogram PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_memory_histogram)

# Machine tests
add_executable(test_machine test_machine.cpp)
target_link_libraries(test_machine PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_machine)

# VIA 6522 tests
add_executable(test_via6522 test_via6522.cpp)
target_link_libraries(test_via6522 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_via6522)

# Memory map tests
add_executable(test_memory_map test_memory_map.cpp)
target_link_libraries(test_memory_map PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_memory_map)

# Clock tests
add_executable(test_clock test_clock.cpp)
target_link_libraries(test_clock PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_clock)

# IRQ aggregator tests
add_executable(test_irq_aggregator test_irq_aggregator.cpp)
target_link_libraries(test_irq_aggregator PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_irq_aggregator)

# Tests requiring ROM files
set(BEEBIUM_ROM_DIR "${CMAKE_SOURCE_DIR}/roms" CACHE PATH "Directory containing BBC Micro ROMs")

# Boot trace tests (uses ProgramCounterHistogram for instruction profiling)
add_executable(test_boot_trace test_boot_trace.cpp)
target_link_libraries(test_boot_trace PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_boot_trace PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_boot_trace)

# Boot tests
add_executable(test_boot test_boot.cpp)
target_link_libraries(test_boot PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_boot PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_boot)

# Boot sequence test (TDD-style boot progression)
add_executable(test_boot_sequence test_boot_sequence.cpp)
target_link_libraries(test_boot_sequence PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_boot_sequence PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_boot_sequence)

# clearRAM test
add_executable(test_clearram test_clearram.cpp)
target_link_libraries(test_clearram PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_clearram PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_clearram)

# Video output tests
add_executable(test_video test_video.cpp)
target_link_libraries(test_video PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_video PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_video)

# SAA5050 teletext chip tests
add_executable(test_saa5050 test_saa5050.cpp)
target_link_libraries(test_saa5050 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_saa5050)

# Addressable latch (IC32) tests
add_executable(test_addressable_latch test_addressable_latch.cpp)
target_link_libraries(test_addressable_latch PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_addressable_latch)

# CRTC 6845 tests
add_executable(test_crtc6845 test_crtc6845.cpp)
target_link_libraries(test_crtc6845 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_crtc6845)

# Video ULA tests
add_executable(test_video_ula test_video_ula.cpp)
target_link_libraries(test_video_ula PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_video_ula)

# Cursor blink tests
add_executable(test_cursor test_cursor.cpp)
target_link_libraries(test_cursor PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_cursor PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_cursor)

# Keyboard input tests
add_executable(test_keyboard_input test_keyboard_input.cpp)
target_link_libraries(test_keyboard_input PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_keyboard_input PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_keyboard_input)

# Keyboard thread safety tests
add_executable(test_keyboard_race test_keyboard_race.cpp)
target_link_libraries(test_keyboard_race PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_keyboard_race)

# gRPC service tests (only if service layer is built)
if(BEEBIUM_BUILD_SERVICE)
    # Video service tests
    add_executable(test_grpc_video test_grpc_video.cpp)
    target_link_libraries(test_grpc_video PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_video PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_video)

    # Keyboard service tests
    add_executable(test_grpc_keyboard test_grpc_keyboard.cpp)
    target_link_libraries(test_grpc_keyboard PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_keyboard PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_keyboard)
endif()
