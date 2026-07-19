# screen-text: turns images into text, given sets of glyphs.
#
# Deliberately standalone. It knows nothing about BBC Micros beyond a built-in
# Acorn glyph set, and nothing at all about emulators. It must not depend on
# beebium_core, the service layer, or anything else of Beebium's: dependencies
# point one way, Beebium to here, never back, so that this can be extracted as
# a separate project. Shared third-party code is fine; Beebium's own is not.

add_library(screentext STATIC
    src/Bitmap.cpp
    src/Glyph.cpp
    src/GlyphIndex.cpp
    src/Read.cpp
    src/BuiltinGlyphSets.cpp
    src/AcornMos120Glyphs.cpp
    include/screentext/Bitmap.hpp
    include/screentext/Glyph.hpp
    include/screentext/Image.hpp
    include/screentext/Read.hpp
    include/screentext/ScreenText.hpp
)

target_include_directories(screentext
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(screentext PUBLIC cxx_std_20)

# The CLI: the same capability without linking, and for offline pipelines over
# directories of screenshots. Image decoding lives here rather than in the
# library, so the library itself carries no third-party dependency at all.
add_executable(screentext-cli
    tools/main.cpp
    tools/ImageIo.cpp
    tools/ImageIo.hpp
    tools/Json.cpp
    tools/Json.hpp
)

set_target_properties(screentext-cli PROPERTIES OUTPUT_NAME screentext)

target_link_libraries(screentext-cli PRIVATE screentext stb_image)
target_include_directories(screentext-cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)

# A latency benchmark, over the library's public interface. Built on request
# rather than by default -- it is a measurement tool, not part of the product.
add_executable(screentext-benchmark EXCLUDE_FROM_ALL tools/benchmark.cpp)
target_link_libraries(screentext-benchmark PRIVATE screentext)

if(BEEBIUM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
