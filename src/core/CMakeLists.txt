# Beebium core library
# Headless BBC Micro emulator core

add_library(beebium_core STATIC
    src/ModelBMemory.cpp
    include/beebium/Types.hpp
    include/beebium/CpuPolicy.hpp
    include/beebium/ModelBMemory.hpp
    include/beebium/Machine.hpp
    include/beebium/Machines.hpp
)

target_include_directories(beebium_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link to 6502 library
target_link_libraries(beebium_core PUBLIC 6502_lib)
