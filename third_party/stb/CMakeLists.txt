add_library(stb_image_write STATIC stb_image_write.cpp)
target_include_directories(stb_image_write PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(stb_image STATIC stb_image.cpp)
target_include_directories(stb_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Suppress warnings in third-party code
foreach(target stb_image_write stb_image)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE -w)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W0)
    endif()
endforeach()
