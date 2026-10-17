# Reading and writing zip archives (ZipArchive.hpp): the only user of the
# vendored miniz, which stays private to it.
add_library(beebium_zip STATIC src/ZipArchive.cpp)
target_include_directories(beebium_zip PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(beebium_zip PRIVATE miniz)
target_compile_features(beebium_zip PUBLIC cxx_std_20)
