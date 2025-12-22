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

# Tests requiring ROM files
set(BEEBIUM_ROM_DIR "/Users/rjs/Code/b2/etc/roms" CACHE PATH "Directory containing BBC Micro ROMs")

# Boot trace tests (uses ProgramCounterHistogram for instruction profiling)
add_executable(test_boot_trace test_boot_trace.cpp)
target_link_libraries(test_boot_trace PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_boot_trace PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_boot_trace)

# clearRAM debug test (uses ProgramCounterHistogram for loop detection)
add_executable(test_clearram_debug test_clearram_debug.cpp)
target_link_libraries(test_clearram_debug PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_clearram_debug PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_clearram_debug)

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

# STA (zp),Y instruction test
add_executable(test_sta_iny test_sta_iny.cpp)
target_link_libraries(test_sta_iny PRIVATE 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_sta_iny)

# Debug and trace tests (require ROM files)

add_executable(test_pagedmode test_pagedmode.cpp)
target_link_libraries(test_pagedmode PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_pagedmode PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_pagedmode)

add_executable(test_cae0_trace test_cae0_trace.cpp)
target_link_libraries(test_cae0_trace PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_cae0_trace PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_cae0_trace)

add_executable(test_rol_trace test_rol_trace.cpp)
target_link_libraries(test_rol_trace PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_rol_trace PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_rol_trace)

add_executable(test_a_trace test_a_trace.cpp)
target_link_libraries(test_a_trace PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_a_trace PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_a_trace)

add_executable(test_keyflow test_keyflow.cpp)
target_link_libraries(test_keyflow PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_keyflow PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_keyflow)

add_executable(test_scancount test_scancount.cpp)
target_link_libraries(test_scancount PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_scancount PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_scancount)

add_executable(test_cae0_scans test_cae0_scans.cpp)
target_link_libraries(test_cae0_scans PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_cae0_scans PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_cae0_scans)

add_executable(test_stack_trace test_stack_trace.cpp)
target_link_libraries(test_stack_trace PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_stack_trace PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_stack_trace)

add_executable(test_cae0_caller test_cae0_caller.cpp)
target_link_libraries(test_cae0_caller PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_cae0_caller PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_cae0_caller)

add_executable(test_vdu_d0 test_vdu_d0.cpp)
target_link_libraries(test_vdu_d0 PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_vdu_d0 PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_vdu_d0)

add_executable(test_mode test_mode.cpp)
target_link_libraries(test_mode PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_mode PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_mode)
