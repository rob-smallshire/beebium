# Copyright 2026 James Sargent <acheton1984@letterboxes.org>
#
# This file is part of AESulator, a plugin for Beebium.
#
# AESulator is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. AESulator is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with AESulator.
# If not, see <https://www.gnu.org/licenses/>.

# AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
# CMakeLists.txt, builds the plugin inside Beebium's tree
# version 2.0.0, 20 November 2026.

# The AES plugin, built inside a Beebium working copy, as Beebium's guide asks (section 7).
# A copy of acorn-aes-coprocessor/ lands in src/extensions/acorn-aes-coprocessor: see tools/plugin-build.sh.

# The plugin's code, built once as a static library that the plugin and the tests both link.
set(AES_SOURCES
    ArmCore.cpp
    AesMemory.cpp
    AesOptions.cpp
    AesTiming.cpp
    beebium/AesTube.cpp
    beebium/AesDebug.cpp
    beebium/AesExtension.cpp
)
add_library(beebium_ext_aes STATIC ${AES_SOURCES})
target_include_directories(beebium_ext_aes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(beebium_ext_aes PUBLIC beebium_core)
# The plugin's version line: AESulator's version, held in manifest.json alone so that it has one
# home, and Beebium's, from its top-level project() line. Beebium reads only the manifest keys it
# knows.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json AES_MANIFEST)
string(JSON AES_VERSION GET "${AES_MANIFEST}" version)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json)
target_compile_definitions(beebium_ext_aes PUBLIC
    AES_VERSION="${AES_VERSION}" AES_BEEBIUM_VERSION="${CMAKE_PROJECT_VERSION}")
set_target_properties(beebium_ext_aes PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The plugin itself, the library Beebium's server loads: only its entry point is exported.
if(BEEBIUM_BUILD_SERVICE)
    add_library(beebium_ext_aes_plugin SHARED plugin_entry.cpp)
    target_link_libraries(beebium_ext_aes_plugin PRIVATE beebium_ext_aes beebium_extension_api)
    set_target_properties(beebium_ext_aes_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "acorn-aes-coprocessor"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
    beebium_finalize_plugin(TARGET beebium_ext_aes_plugin NAME acorn-aes-coprocessor)
endif()

# The counting wrapper, a test build only: the same plugin with
# beebium/TubeLog.hpp compiled in, kept out of the server's own extensions folder and reached
# with --extension-dir, which overrides a matching cli name (Beebium's cli.md).
if(BEEBIUM_BUILD_SERVICE AND BEEBIUM_BUILD_TESTS)
    set(AES_LOG_DIR ${CMAKE_CURRENT_BINARY_DIR}/tube-log/acorn-aes-coprocessor)
    add_library(beebium_ext_aes_log_plugin SHARED
        ${AES_SOURCES}
        plugin_entry.cpp
        ${CMAKE_SOURCE_DIR}/src/core/extension-api/PluginAbiVersion.cpp
    )
    target_compile_definitions(beebium_ext_aes_log_plugin PRIVATE AES_TUBE_LOG
        AES_VERSION="${AES_VERSION}" AES_BEEBIUM_VERSION="${CMAKE_PROJECT_VERSION}")
    target_include_directories(beebium_ext_aes_log_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(beebium_ext_aes_log_plugin PRIVATE beebium_core beebium_extension_api)
    set_target_properties(beebium_ext_aes_log_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "acorn-aes-coprocessor"
        LIBRARY_OUTPUT_DIRECTORY ${AES_LOG_DIR}
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
    configure_file(manifest.json ${AES_LOG_DIR}/manifest.json COPYONLY)
endif()

# The server-side tests of the AES machine, run by Beebium's test runner.
if(BEEBIUM_BUILD_TESTS)
    add_executable(test_aes_machine tests/machine_test.cpp)
    target_link_libraries(test_aes_machine PRIVATE beebium_ext_aes Catch2::Catch2WithMain)
    target_compile_definitions(test_aes_machine PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
        AES_ROM_FACTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/rom-facts")
    catch_discover_tests(test_aes_machine)
endif()
