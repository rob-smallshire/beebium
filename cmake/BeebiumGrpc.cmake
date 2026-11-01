# BeebiumGrpc.cmake - find gRPC, Protobuf and OpenSSL, and the code generators
#
# Shared by the root build (servers, service layer, tests) and by clients
# built as projects of their own (clients/linux), so that every C++ consumer
# of the protos finds its dependencies, and generates its stubs, the same way.
#
# Sets PROTOC_EXECUTABLE and GRPC_CPP_PLUGIN, ensures the gRPC::grpc++ and
# protobuf::libprotobuf targets exist, and includes BeebiumProto.cmake for
# beebium_compile_proto().

# Find gRPC and Protobuf
# Try CONFIG mode first (macOS Homebrew), fall back to Module mode (Linux apt)
find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
    find_package(Protobuf REQUIRED)
endif()

# gRPC's CONFIG-mode imported targets (e.g. Homebrew's) name OpenSSL::SSL and
# OpenSSL::Crypto in their link interface, so OpenSSL must be found before
# gRPC is or processing gRPCTargets.cmake fails with a missing target.
# Harmless on the Linux pkg-config / Windows vcpkg paths below.
#
# On macOS with Homebrew, OpenSSL is keg-only and not on the default search
# path, so the find_package(OpenSSL) below would silently miss it, leaving
# OpenSSL::SSL undefined and the Homebrew gRPC config erroring. Hint the
# Homebrew prefix. Guarded to the Homebrew path only: skipped under the
# vcpkg toolchain (which supplies its own OpenSSL) and when the caller has
# already set OPENSSL_ROOT_DIR, so it never redirects those builds.
if(APPLE AND NOT VCPKG_TOOLCHAIN AND NOT DEFINED OPENSSL_ROOT_DIR)
    find_program(BREW_EXECUTABLE brew)
    if(BREW_EXECUTABLE)
        execute_process(
            COMMAND ${BREW_EXECUTABLE} --prefix openssl@3
            OUTPUT_VARIABLE _brew_openssl_prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_brew_openssl_prefix AND EXISTS "${_brew_openssl_prefix}")
            set(OPENSSL_ROOT_DIR "${_brew_openssl_prefix}")
        endif()
    endif()
endif()

find_package(OpenSSL QUIET)

find_package(gRPC CONFIG QUIET)
if(NOT gRPC_FOUND)
    if(WIN32)
        # On Windows, gRPC must be found via CONFIG mode (vcpkg)
        message(FATAL_ERROR
            "gRPC not found. On Windows, use vcpkg:\n"
            "  vcpkg install grpc:x64-windows\n"
            "Then configure with: -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake"
        )
    else()
        # On Linux, gRPC may need to be found via pkg-config
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GRPC REQUIRED grpc++ grpc)

        # Create imported target for gRPC if not already defined
        if(NOT TARGET gRPC::grpc++)
            add_library(gRPC::grpc++ INTERFACE IMPORTED)
            set_target_properties(gRPC::grpc++ PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${GRPC_INCLUDE_DIRS}"
                INTERFACE_LINK_LIBRARIES "${GRPC_LIBRARIES}"
            )
        endif()
    endif()
endif()

# Get the protoc executable
if(TARGET protobuf::protoc)
    get_target_property(PROTOC_EXECUTABLE protobuf::protoc LOCATION)
else()
    find_program(PROTOC_EXECUTABLE protoc REQUIRED)
endif()

# Get the grpc_cpp_plugin executable
if(TARGET gRPC::grpc_cpp_plugin)
    get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin LOCATION)
else()
    find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)
endif()

# Proto compilation helper
include(${CMAKE_CURRENT_LIST_DIR}/BeebiumProto.cmake)
