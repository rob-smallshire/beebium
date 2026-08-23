# CPack configuration for the self-contained Linux server bundle.
#
# Produces, from the same install() rules the bundle is built on:
#   - a .deb that lays the relocatable tree under /opt/beebium and symlinks the
#     four server binaries into /usr/bin (via the maintainer scripts),
#   - an .rpm that does the same for the Fedora / RHEL family (%post/%preun
#     scriptlets), and
#   - a plain .tar.gz of the same tree for distros with no native package here.
#
# Intended for the Linux bundle build; the TGZ generator is also configured
# elsewhere (e.g. macOS) so the layout can be verified, but the DEB and RPM
# generators are only added on Linux (they need dpkg / rpmbuild).

set(CPACK_PACKAGE_NAME "beebium-server")
set(CPACK_PACKAGE_VENDOR "Robert Smallshire")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Beebium headless BBC Micro emulator server (self-contained)")
set(CPACK_PACKAGE_CONTACT "Robert Smallshire <robert@smallshire.org.uk>")

# The install layout differs per generator: the .deb lays a /opt/beebium system
# tree, while the .tar.gz is a relocatable single directory the user extracts
# anywhere. CPack evaluates this config file once per generator (with
# CPACK_GENERATOR set) to pick the prefix and top-level wrapper for each.
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/BeebiumCPackOptions.cmake")

# OS + architecture tokens for the package file name. On Linux the arch is
# normalised to the Debian label (amd64/arm64) so the .deb and .tar.gz match the
# .deb's auto-detected Architecture field. On macOS the tarball uses the Apple
# arch names (arm64 / x86_64), which is also what the platform wheel tags expect.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_beebium_pkg_os "macos")
    if(CMAKE_OSX_ARCHITECTURES)
        set(_beebium_pkg_arch "${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(_beebium_pkg_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
else()
    set(_beebium_pkg_os "linux")
    set(_beebium_pkg_arch "${CMAKE_SYSTEM_PROCESSOR}")
    if(_beebium_pkg_arch STREQUAL "x86_64")
        set(_beebium_pkg_arch "amd64")
    elseif(_beebium_pkg_arch STREQUAL "aarch64")
        set(_beebium_pkg_arch "arm64")
    endif()
endif()

# Base package file name used by all generators: beebium-server-<ver>-<os>-<arch>.
# The DEB generator overrides this with the canonical Debian name below. Setting
# CPACK_PACKAGE_FILE_NAME (rather than CPACK_ARCHIVE_FILE_NAME) is what reliably
# names the archive across CPack versions.
set(CPACK_PACKAGE_FILE_NAME
    "beebium-server-${PROJECT_VERSION}-${_beebium_pkg_os}-${_beebium_pkg_arch}")

# Archive (TGZ): a relocatable single-directory bundle (see the per-generator
# prefix/wrapper in BeebiumCPackOptions.cmake). The DEB generator is added below
# on Linux.
set(CPACK_GENERATOR "TGZ")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND CPACK_GENERATOR "DEB")

    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Robert Smallshire <robert@smallshire.org.uk>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "otherosfs")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/rob-smallshire/beebium")
    # Derive runtime dependencies from the binaries' actual dynamic links
    # (libc6, libstdc++6, libgcc-s1). gRPC/protobuf are statically linked and
    # contribute no package dependencies.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # Canonical Debian file name: beebium-server_<version>_<arch>.deb, with the
    # architecture auto-detected via dpkg --print-architecture.
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
    # postinst/prerm create and remove the /usr/bin -> /opt/beebium/bin symlinks.
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_SOURCE_DIR}/packaging/debian/postinst"
        "${CMAKE_SOURCE_DIR}/packaging/debian/prerm")

    # RPM sibling of the .deb for the Fedora / RHEL family (see
    # docs/plans/linux-rpm-packaging.md). Same /opt/beebium payload and /usr/bin
    # symlinks; only the arch naming (x86_64/aarch64) and scriptlet argument
    # semantics differ. rpmbuild is provided by the `rpm` package in the build
    # image; RPM's find-requires derives the same base-library dependencies (and
    # the versioned glibc floor) automatically from the ELF binaries.
    list(APPEND CPACK_GENERATOR "RPM")

    # Canonical file name: beebium-server-<version>-1.<arch>.rpm.
    set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
    set(CPACK_RPM_PACKAGE_LICENSE "GPLv3+")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Emulators")
    set(CPACK_RPM_PACKAGE_URL "https://github.com/rob-smallshire/beebium")
    # A fixed /opt/beebium system install (the scriptlets hardcode that path),
    # not a user-relocatable package -- matches the .deb.
    set(CPACK_RPM_PACKAGE_RELOCATABLE OFF)
    # %post/%preun create and remove the /usr/bin -> /opt/beebium/bin symlinks.
    # RPM scriptlets take $1 = instance count (not the .deb's action word), so
    # these are distinct from the Debian maintainer scripts.
    set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE
        "${CMAKE_SOURCE_DIR}/packaging/rpm/postinstall")
    set(CPACK_RPM_PRE_UNINSTALL_SCRIPT_FILE
        "${CMAKE_SOURCE_DIR}/packaging/rpm/preuninstall")
endif()

include(CPack)
