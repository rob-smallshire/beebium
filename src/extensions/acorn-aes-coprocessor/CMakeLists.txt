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
# home. Beebium reads only the manifest keys it knows.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json AES_MANIFEST)
string(JSON AES_VERSION GET "${AES_MANIFEST}" version)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json)
target_compile_definitions(beebium_ext_aes PUBLIC AES_VERSION="${AES_VERSION}")
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
        AES_VERSION="${AES_VERSION}")
    target_include_directories(beebium_ext_aes_log_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(beebium_ext_aes_log_plugin PRIVATE beebium_core beebium_extension_api)
    set_target_properties(beebium_ext_aes_log_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "acorn-aes-coprocessor"
        # Beside its manifest on every platform: a Windows DLL is a RUNTIME output, and the
        # generator expression keeps a multi-config generator from adding a Release/ folder.
        LIBRARY_OUTPUT_DIRECTORY "$<1:${AES_LOG_DIR}>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${AES_LOG_DIR}>"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
    configure_file(manifest.json ${AES_LOG_DIR}/manifest.json COPYONLY)
    file(COPY roms DESTINATION ${AES_LOG_DIR})
endif()

# The server-side tests of the AES machine, run by Beebium's test runner.
if(BEEBIUM_BUILD_TESTS)
    add_executable(test_aes_machine tests/machine_test.cpp)
    target_link_libraries(test_aes_machine PRIVATE beebium_ext_aes Catch2::Catch2WithMain)
    set(AES_BUNDLED_ROM_FILEPATH ${CMAKE_CURRENT_SOURCE_DIR}/roms/acorn-aes-executive_1_00.rom)
    target_compile_definitions(test_aes_machine PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
        AES_ROM_FACTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/rom-facts"
        AES_BUNDLED_ROM_FILEPATH="${AES_BUNDLED_ROM_FILEPATH}")
    catch_discover_tests(test_aes_machine)

    # The plugin's own tests, on its own runner (tests/test_main.cpp), registered as one test so
    # that plain ctest runs them; tests tied to the Executive ROM skip when
    # BEEBIUM_AES_ROM_FILEPATH is unset.
    file(GLOB AES_UNIT_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)
    add_executable(test_aes_plugin ${AES_UNIT_TEST_SOURCES})
    target_link_libraries(test_aes_plugin PRIVATE beebium_ext_aes beebium_extension_api)
    target_compile_definitions(test_aes_plugin PRIVATE
        AES_ROM_FACTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/rom-facts"
        AES_BUNDLED_ROM_FILEPATH="${AES_BUNDLED_ROM_FILEPATH}")
    add_test(NAME aes_plugin_tests COMMAND test_aes_plugin)
    set_tests_properties(aes_plugin_tests PROPERTIES
        ENVIRONMENT "AES_PLUGIN_DIR=${CMAKE_CURRENT_SOURCE_DIR}")

    # Windows has no RPATH: these executables load beebium_extension_api.dll from their own
    # directory, and the build copies that DLL into tests/<config> beside Beebium's own test
    # executables (src/core/extension-api/CMakeLists.txt). Build them there too, so ctest's
    # discovery can run them.
    if(WIN32)
        set_target_properties(test_aes_machine test_aes_plugin PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
    endif()

    # As tests/CMakeLists.txt does for Beebium's own test executables: the tests poke registers
    # and bytes with integer literals, which MSVC /W4 reports as narrowing (C4244, C4267) and
    # Catch2's macros as bool/int mixes (C4805). test_aes_plugin also truncates constants into
    # bytes on purpose (C4310).
    if(MSVC)
        foreach(aes_test_target test_aes_machine test_aes_plugin)
            target_compile_options(${aes_test_target} PRIVATE /wd4244 /wd4267 /wd4805)
        endforeach()
        target_compile_options(test_aes_plugin PRIVATE /wd4310)
    endif()
endif()
