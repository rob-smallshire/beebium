# Beebium core library
# Headless BBC Micro emulator core

add_library(beebium_core STATIC
    src/Via6522.cpp
    include/beebium/Types.hpp
    include/beebium/CpuPolicy.hpp
    include/beebium/ModelBHardware.hpp
    include/beebium/Machine.hpp
    include/beebium/Machines.hpp
    include/beebium/Via6522.hpp
    include/beebium/Via6522Types.hpp
    include/beebium/MemoryMap.hpp
    include/beebium/BankBinding.hpp
    include/beebium/devices/Ram.hpp
    include/beebium/devices/Rom.hpp
    include/beebium/devices/EmptySlot.hpp
    include/beebium/devices/BankedMemory.hpp
)

target_include_directories(beebium_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link to 6502 library
target_link_libraries(beebium_core PUBLIC 6502_lib)
