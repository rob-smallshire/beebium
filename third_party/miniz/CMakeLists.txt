# miniz 3.0.2 (https://github.com/richgel999/miniz, MIT; see LICENSE), the
# amalgamated miniz.c/miniz.h from the release archive miniz-3.0.2.zip,
# unmodified. Only beebium_zip (src/zip) uses it.
add_library(miniz STATIC miniz.c)
target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# No stdio: beebium_zip reads and writes archives through its own callbacks,
# which open files by std::filesystem::path (wide on Windows). No time: an
# archive written from the same members is byte-identical, whatever the
# clock or time zone. No zlib names: zlib is linked too, through gRPC.
target_compile_definitions(miniz PUBLIC
    MINIZ_NO_STDIO
    MINIZ_NO_TIME
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES
)
set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Suppress warnings in third-party code
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(miniz PRIVATE -w)
elseif(MSVC)
    target_compile_options(miniz PRIVATE /W0)
endif()
