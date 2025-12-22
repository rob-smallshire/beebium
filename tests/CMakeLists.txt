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
