# screen-text tests.
#
# Fixtures rather than an emulator: images are composed in the tests by
# stamping known glyphs at known positions, so every case is constructed
# exactly and a recognition bug is reproducible without a machine.

set(SCREENTEXT_TEST_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_executable(test_screentext_bitmap test_bitmap.cpp)
target_link_libraries(test_screentext_bitmap PRIVATE screentext Catch2::Catch2WithMain)
catch_discover_tests(test_screentext_bitmap)

add_executable(test_screentext_glyphs test_glyphs.cpp)
target_link_libraries(test_screentext_glyphs PRIVATE screentext Catch2::Catch2WithMain)
catch_discover_tests(test_screentext_glyphs)

add_executable(test_screentext_read test_read.cpp)
target_link_libraries(test_screentext_read PRIVATE screentext Catch2::Catch2WithMain)
target_include_directories(test_screentext_read PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
catch_discover_tests(test_screentext_read)

# The CLI is exercised through its own interface -- run as a process, with its
# output parsed -- rather than by calling the library it wraps.
add_executable(test_screentext_cli test_cli.cpp)
target_link_libraries(test_screentext_cli PRIVATE screentext Catch2::Catch2WithMain)
target_include_directories(test_screentext_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Images the CLI tests build for themselves go to the build tree, so running
# the tests never writes into the source tree.
set(SCREENTEXT_TEST_SCRATCH_DIR "${CMAKE_CURRENT_BINARY_DIR}/scratch")
file(MAKE_DIRECTORY "${SCREENTEXT_TEST_SCRATCH_DIR}")

target_compile_definitions(test_screentext_cli PRIVATE
    SCREENTEXT_CLI_PATH="$<TARGET_FILE:screentext-cli>"
    SCREENTEXT_TEST_FIXTURES_DIR="${SCREENTEXT_TEST_FIXTURES_DIR}"
    SCREENTEXT_TEST_SCRATCH_DIR="${SCREENTEXT_TEST_SCRATCH_DIR}"
)
add_dependencies(test_screentext_cli screentext-cli)
catch_discover_tests(test_screentext_cli)
