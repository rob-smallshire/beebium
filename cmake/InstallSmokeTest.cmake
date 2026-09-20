# InstallSmokeTest.cmake - verify a `cmake --install` tree is actually runnable.
#
# Driven by the "install_smoke" CTest registered in tests/CMakeLists.txt. Runs
# a real install into a throwaway staging prefix and then exercises the three
# things that distinguish an installed tree from the build tree:
#
#   1. the extension ABI shared libraries resolve via the server's INSTALL_RPATH
#      (the binary loads at all), and the plugin tree is discovered from
#      <prefix>/bin/extensions  -- checked via `list-extensions`;
#   2. ROMs resolve through the <prefix>/share/beebium/roms fallback and the
#      gRPC server reaches "Listening on port" -- checked by booting briefly.
#
# This guards every downstream package format (.deb, Homebrew, tarball): they
# all stand on the same install rules and runtime discovery paths.
#
# Required -D arguments:
#   BINARY_DIR  - the CMake build directory to install from
#   CONFIG      - build configuration for multi-config generators (may be empty)
#   EXE_SUFFIX  - executable suffix (".exe" on Windows, empty elsewhere)

if(NOT BINARY_DIR)
    message(FATAL_ERROR "InstallSmokeTest: BINARY_DIR is required")
endif()

set(stage "${BINARY_DIR}/install-smoke-stage")
set(prefix "${stage}/opt/beebium")
file(REMOVE_RECURSE "${stage}")

# --- 1. install into the staging prefix -------------------------------------
set(install_args --install "${BINARY_DIR}" --prefix "${prefix}")
if(CONFIG)
    list(APPEND install_args --config "${CONFIG}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" ${install_args}
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "cmake --install failed (${rc}):\n${out}\n${err}")
endif()

set(server "${prefix}/bin/beebium-model-b${EXE_SUFFIX}")
if(NOT EXISTS "${server}")
    message(FATAL_ERROR "installed server binary missing: ${server}")
endif()

# --- 2. list-extensions: ABI lib load (RPATH) + plugin discovery ------------
execute_process(
    COMMAND "${server}" list-extensions
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "list-extensions failed (${rc}) -- ABI lib or plugin "
                        "resolution broken in the installed tree:\n${out}\n${err}")
endif()
# Each of these is a SHARED-library plugin deployed under bin/extensions/<name>;
# their presence proves the installed plugin tree was found and loaded. Covers
# one of each kind: a 1MHz-bus device (scsi-hdd), a user-port device (acorn-rtc),
# an econet transport (piconet), and the network-serial extensions, whose extra
# runtime deps (gRPC for rpc-serial) must also resolve from the installed tree.
foreach(plugin scsi-hdd acorn-rtc piconet
               ip232-serial rpc-serial loopback-serial
               rfc2217-client-serial rfc2217-server-serial)
    if(NOT out MATCHES "${plugin}")
        message(FATAL_ERROR "plugin '${plugin}' not discovered from the installed "
                            "tree (bin/extensions):\n${out}")
    endif()
endforeach()

# --- 3. boot: ROM discovery via share fallback + gRPC bring-up --------------
# The server runs until interrupted, so bound it with a TIMEOUT (which kills the
# child and returns a non-zero result -- expected) and assert on the output
# instead. "Listening on port" is emitted with std::endl, so it is flushed
# before the serve loop blocks and is reliably captured here.
execute_process(
    COMMAND "${server}" start
            --mos acorn-mos_1_20.rom
            --sideways slot=15:type=rom:image=bbc-basic_2.rom
            --port 0
    TIMEOUT 20
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT "${out}${err}" MATCHES "Listening on port")
    message(FATAL_ERROR "installed server did not reach 'Listening on port' "
                        "(result=${rc}) -- ROM discovery or gRPC bring-up "
                        "failed:\n${out}\n${err}")
endif()

# --- 4. clean up ------------------------------------------------------------
file(REMOVE_RECURSE "${stage}")
message(STATUS "install smoke test passed")
