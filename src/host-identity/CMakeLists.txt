# beebium_host_identity - which host a process is running on
#
# Header-only and free of the emulator core, so that servers and clients
# derive the host fingerprint (SystemInfo.host_fingerprint) from the one
# recipe. See docs/frontend-local-server-gating.md.

add_library(beebium_host_identity INTERFACE)

target_include_directories(beebium_host_identity
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(beebium_host_identity INTERFACE cxx_std_20)

# HostFingerprint.hpp hashes with OpenSSL's SHA256. Where OpenSSL was found
# as a CMake package, link it; otherwise it arrives with gRPC's link line
# (the Linux pkg-config path).
if(TARGET OpenSSL::Crypto)
    target_link_libraries(beebium_host_identity INTERFACE OpenSSL::Crypto)
endif()
