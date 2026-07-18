# Extension API shared library (must come before beebium_core)
add_subdirectory(extension-api)

# Beebium core library
# Headless BBC Micro emulator core

add_library(beebium_core STATIC
    src/Via6522.cpp
    src/Sn76489.cpp
    src/KeyboardMapping.cpp
    src/TeletextText.cpp
    src/TextTranslation.cpp
    src/TypeAheadQueue.cpp
    src/disc/formats/SsdFormatHandler.cpp
    src/disc/formats/AdfsFormatHandler.cpp
    src/disc/formats/HfeFormatHandler.cpp
    src/econet/AunBackend.cpp
    src/TubeUla.cpp
    src/ParasiteCpu.cpp
    src/ParasiteRunner.cpp
    src/Expression.cpp
    # Serial-port enumeration -- platform-conditional; only one .cpp
    # compiles per build. Both are listed so IDEs see them.
    $<$<NOT:$<BOOL:${WIN32}>>:${CMAKE_CURRENT_SOURCE_DIR}/src/EnumeratePortsPosix.cpp>
    $<$<BOOL:${WIN32}>:${CMAKE_CURRENT_SOURCE_DIR}/src/EnumeratePortsWin32.cpp>
    include/beebium/serial/EnumeratePorts.hpp
    # Host serial transport primitives (promoted from the piconet extension).
    # POSIX compiles PosixSerialPort + PtyMaster; Windows compiles Win32SerialPort.
    $<$<NOT:$<BOOL:${WIN32}>>:${CMAKE_CURRENT_SOURCE_DIR}/src/PosixSerialPort.cpp>
    $<$<NOT:$<BOOL:${WIN32}>>:${CMAKE_CURRENT_SOURCE_DIR}/src/PtyMaster.cpp>
    $<$<BOOL:${WIN32}>:${CMAKE_CURRENT_SOURCE_DIR}/src/Win32SerialPort.cpp>
    include/beebium/serial/HostSerialPort.hpp
    include/beebium/serial/PosixSerialPort.hpp
    include/beebium/serial/Win32SerialPort.hpp
    include/beebium/serial/PtyMaster.hpp
    # Shared cross-platform TCP transport (network-backed serial devices).
    src/net/TcpClientSerialPort.cpp
    src/net/TcpServerSerialPort.cpp
    include/beebium/net/SocketPlatform.hpp
    include/beebium/net/TcpClientSerialPort.hpp
    include/beebium/net/TcpServerSerialPort.hpp
    include/beebium/net/EndpointUrl.hpp
    include/beebium/Types.hpp
    include/beebium/CpuPolicy.hpp
    include/beebium/ModelBHardware.hpp
    include/beebium/Machine.hpp
    include/beebium/Machines.hpp
    include/beebium/Via6522.hpp
    include/beebium/Via6522Types.hpp
    include/beebium/MemoryMap.hpp
    include/beebium/BankBinding.hpp
    include/beebium/devices/Mc6850.hpp
    include/beebium/serial/SerialDevice.hpp
    include/beebium/serial/SerialUla.hpp
    include/beebium/serial/SerialSocket.hpp
    include/beebium/serial/SerialConcepts.hpp
    include/beebium/devices/Ram.hpp
    include/beebium/devices/Rom.hpp
    include/beebium/devices/EmptySlot.hpp
    include/beebium/devices/BankedMemory.hpp
    include/beebium/devices/Sn76489.hpp
    include/beebium/AudioBuffer.hpp
)

target_include_directories(beebium_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# Link to 6502 library
target_link_libraries(beebium_core PUBLIC 6502_lib)

# Link to extension API shared library (provides PeripheralExtension,
# ExtensionContext, OneMHzBusPort, UserPort, PluginLoader, etc.)
target_link_libraries(beebium_core PUBLIC beebium_extension_api)
