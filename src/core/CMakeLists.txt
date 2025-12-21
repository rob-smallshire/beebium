# Beebium core library
# Headless BBC Micro emulator core

add_library(beebium_core STATIC
    src/Memory.cpp
    src/BeebiumState.cpp
    src/Beebium.cpp
    include/beebium/Types.hpp
    include/beebium/Memory.hpp
    include/beebium/BeebiumState.hpp
    include/beebium/Beebium.hpp
)

target_include_directories(beebium_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link to 6502 library
target_link_libraries(beebium_core PUBLIC 6502_lib)
