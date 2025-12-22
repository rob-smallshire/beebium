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
