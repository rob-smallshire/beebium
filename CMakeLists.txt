cmake_minimum_required(VERSION 3.16)

project(beebium
    VERSION 0.1.5
    DESCRIPTION "BBC Micro emulator with headless core and IPC architecture"
    LANGUAGES C CXX
)

# Position-independent code for all targets (required on Linux where static
# libraries are linked into plugin shared libraries)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Serialise the most memory-hungry compiles into a depth-1 Ninja job pool. The
# per-variant server mains each instantiate the full Machine<Hardware> template
# against the static gRPC/protobuf headers and peak at ~2.5-3 GB; building
# several at once OOM-kills the compiler on memory-limited hosts (e.g. an 8 GB
# Docker VM) even though the rest of the tree parallelises fine. Server targets
# opt in via JOB_POOL_COMPILE; the property is ignored by non-Ninja generators.
set_property(GLOBAL PROPERTY JOB_POOLS beebium_heavy_compile=1)

# C++ Standard (C++20 for concepts, constexpr improvements, std::span)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C Standard (for 6502 library)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror=return-type
    )

    # Additional warnings for C++
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
        $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
    )
elseif(MSVC)
    add_compile_options(
        /W4
        /permissive-        # Strict conformance mode
        /Zc:__cplusplus     # Correct __cplusplus macro value
        /utf-8              # Source and execution charset UTF-8
        /wd4324             # Structure padding due to alignas (intentional for cache-line alignment)
        /wd4251             # dll-interface for std:: members; unavoidable in the generated protobuf headers and benign with a single shared runtime
        /wd4275             # non dll-interface base class; same origin and rationale as 4251
    )
    # MSVC deprecates the standard C library functions (fopen, fread, getenv,
    # ...) in favour of its non-portable _s variants. These are portable
    # standard functions, so silence the deprecation rather than fork the code.
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
    )
endif()

# Sanitizers for Debug builds
option(BEEBIUM_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
if(BEEBIUM_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# ThreadSanitizer for hunting data races and lock-order inversions. Kept
# separate from BEEBIUM_ENABLE_SANITIZERS because TSan cannot be combined with
# AddressSanitizer in the same build.
option(BEEBIUM_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
if(BEEBIUM_ENABLE_TSAN)
    if(BEEBIUM_ENABLE_SANITIZERS)
        message(FATAL_ERROR "BEEBIUM_ENABLE_TSAN cannot be combined with BEEBIUM_ENABLE_SANITIZERS (ThreadSanitizer conflicts with AddressSanitizer)")
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
        add_link_options(-fsanitize=thread)
    endif()
endif()

# Code coverage (use as guidance, not a target)
option(BEEBIUM_ENABLE_COVERAGE "Enable code coverage reporting" OFF)
if(BEEBIUM_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
        add_link_options(--coverage)
    endif()
endif()

# ROM directory for development and installation
set(BEEBIUM_ROM_DIR "${CMAKE_SOURCE_DIR}/roms" CACHE PATH "Source ROM directory")

# Testing
option(BEEBIUM_BUILD_TESTS "Build tests" ON)
if(BEEBIUM_BUILD_TESTS)
    enable_testing()

    # Fetch Catch2 (archive download, no git clone)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.0.tar.gz
        URL_HASH SHA256=f6d4f8d78a9b59ec72a81d49f58d18eb317372ac07f8d9432710a079e69fd66a
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(CTest)
    include(Catch)

    # Every Catch2 test treats a legitimate skip -- a fixture or external tool
    # the runner cannot provide -- as skipped, not failed. Catch2 exits 4 for a
    # skip-only run, and CTest's SKIP_RETURN_CODE property maps that to
    # "Skipped". Rather than repeat it at ~200 call sites (and forget it at most
    # of them), the stock catch_discover_tests is wrapped once, here at the root
    # so every test-registering subdirectory inherits it -- tests/, and the
    # catch_discover_tests calls under src/ too.
    #
    # SKIP_RETURN_CODE is merged into any PROPERTIES a call site already passes:
    # a second PROPERTIES keyword would let cmake_parse_arguments keep only the
    # last, silently dropping e.g. a RESOURCE_LOCK. The original command remains
    # callable as _catch_discover_tests (CMake's command-override convention).
    function(catch_discover_tests TARGET)
        cmake_parse_arguments(_cdt "" "" "PROPERTIES" ${ARGN})
        _catch_discover_tests(${TARGET} ${_cdt_UNPARSED_ARGUMENTS}
            PROPERTIES ${_cdt_PROPERTIES} SKIP_RETURN_CODE 4)
    endfunction()

    # Defer Catch2's test discovery from build time to ctest time on Windows.
    # The default POST_BUILD mode runs each test executable once during the
    # build to enumerate its cases, but the test .exes load beebium's own
    # beebium_extension_api.dll (a SHARED library -- the plugin ABI boundary),
    # which the CI job copies next to them only just before invoking ctest. At
    # build time it is absent, so POST_BUILD discovery fails with exit code
    # 0xc0000135 (STATUS_DLL_NOT_FOUND). PRE_TEST runs discovery at `ctest`
    # invocation instead, by which time the DLL is in place. (The static-md
    # build links gRPC/protobuf/abseil into the .exes, so vcpkg DLLs are no
    # longer a factor; the remaining runtime DLL is our extension API.)
    if(WIN32)
        set(CMAKE_CATCH_DISCOVER_TESTS_DISCOVERY_MODE "PRE_TEST")
    endif()
endif()

# gRPC Service Layer
option(BEEBIUM_BUILD_SERVICE "Build gRPC service layer" ON)
option(BEEBIUM_BUILD_SERVER "Build beebium-server executable" ON)

# nlohmann-json for preset file parsing (header-only). Prefer an installed copy
# (Homebrew's nlohmann-json, a distro package, or vcpkg) via find_package, and
# fall back to fetching a pinned release only when none is present. Homebrew's
# build sandbox forbids FetchContent network access, so on that path the
# packaged copy must be found. Both paths expose nlohmann_json::nlohmann_json.
find_package(nlohmann_json 3.11 CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
        URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

if(BEEBIUM_BUILD_SERVICE)
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
    include(${CMAKE_SOURCE_DIR}/cmake/BeebiumProto.cmake)
endif()

# Plugin post-build deployment helper (beebium_finalize_plugin).
include(${CMAKE_SOURCE_DIR}/cmake/BeebiumPlugin.cmake)

# Third-party libraries
add_subdirectory(third_party/stb)

# Subdirectories
add_subdirectory(src/6502)
# Standalone: depends on nothing else in this repository, and nothing in this
# repository may depend on it except by choice. Listed before the core so that
# the one-way direction of the dependency is plain.
add_subdirectory(src/screen-text)
add_subdirectory(src/core)
add_subdirectory(src/extensions)
add_subdirectory(src/discovery)

if(BEEBIUM_BUILD_SERVICE)
    add_subdirectory(src/service)
endif()

if(BEEBIUM_BUILD_SERVER)
    add_subdirectory(src/server)
endif()

if(BEEBIUM_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# CPack packaging for the self-contained Linux server bundle (.deb + .tar.gz).
# Configured only with the server build; inert until `cpack` is invoked.
if(BEEBIUM_BUILD_SERVER)
    include(${CMAKE_SOURCE_DIR}/cmake/BeebiumPackaging.cmake)
endif()

# Convenience targets for the macOS frontend, so it shares the uniform
# `cmake --build . --target <name>` interface as everything else. They delegate
# to scripts/build-macos-app.sh, which builds the servers and embeds them in the
# app bundle (see the script header and README.md). macOS-only.
#   cmake --build . --target macos-app   (or `make macos-app` from the build dir)
#   cmake --build . --target macos-run   builds and launches the app
if(APPLE)
    # Pass BUILD_DIR so the script builds and embeds servers from this
    # configured tree, not its default ./build (matters for out-of-tree or
    # release build dirs).
    add_custom_target(macos-app
        COMMAND ${CMAKE_COMMAND} -E env BUILD_DIR=${CMAKE_BINARY_DIR}
                ${CMAKE_SOURCE_DIR}/scripts/build-macos-app.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        USES_TERMINAL
        COMMENT "Building the macOS app with freshly built embedded servers")
    add_custom_target(macos-run
        COMMAND ${CMAKE_COMMAND} -E env BUILD_DIR=${CMAKE_BINARY_DIR}
                ${CMAKE_SOURCE_DIR}/scripts/build-macos-app.sh --run
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        USES_TERMINAL
        COMMENT "Building and launching the macOS app")
endif()
