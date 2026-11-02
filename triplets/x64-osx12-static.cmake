# x64-osx-static at the macOS server bundle's deployment floor. The bundle's
# static deps are compiled for this floor, not the runner's macOS, so the floor
# holds in every object linked into the binary (#243). Keep it equal to the
# x86_64 deployment_target in .github/workflows/macos-bundle.yml.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 12.0)
set(VCPKG_BUILD_TYPE release)
