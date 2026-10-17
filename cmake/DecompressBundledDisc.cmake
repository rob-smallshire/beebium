# Extract a bundled SCSI hard-disc master (a .tar.xz carrying the versioned
# .dat + .dsc) into the build-tree disc directory, then mark the extracted
# masters read-only. Invoked via `cmake -DTARBALL=... -DOUTDIR=... -P`;
# -DREAD_ONLY=OFF leaves the extracted files writable (a disc extracted into
# a preset being put together, which is archived and then removed).
#
# The images are shipped read-only MASTERS; the server copies them to a
# per-user working copy on first use (see DiscPaths) and never opens a master
# writable. The read-only chmod here is belt-and-braces on top of that
# structural guarantee -- it also lets an incremental rebuild replace an
# already-read-only master (we restore write before re-extracting).

if(NOT DEFINED TARBALL OR NOT DEFINED OUTDIR)
    message(FATAL_ERROR "DecompressBundledDisc: TARBALL and OUTDIR are required")
endif()
if(NOT DEFINED READ_ONLY)
    set(READ_ONLY ON)
endif()

file(MAKE_DIRECTORY "${OUTDIR}")

# Incremental builds: a prior run left the extracted masters read-only.
# Restore owner-write so extraction can replace them (file(CHMOD) is 3.19+;
# on older CMake, libarchive's unlink-before-create handles replacement).
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.19)
    file(GLOB _existing "${OUTDIR}/*.dat" "${OUTDIR}/*.dsc")
    foreach(_f ${_existing})
        file(CHMOD "${_f}" PERMISSIONS OWNER_READ OWNER_WRITE)
    endforeach()
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xJf "${TARBALL}"
    WORKING_DIRECTORY "${OUTDIR}"
    RESULT_VARIABLE _extract_rc)
if(NOT _extract_rc EQUAL 0)
    message(FATAL_ERROR "DecompressBundledDisc: failed to extract ${TARBALL}")
endif()

# The tarball records a fixed, historical mtime for reproducibility, which
# would leave the extracted masters older than the tarball forever: the build
# would extract again, and regenerate every preset naming the disc, each time.
# Stamp them with the extraction time so they are up to date until the tarball
# changes.
file(GLOB _extracted "${OUTDIR}/*.dat" "${OUTDIR}/*.dsc")
foreach(_f ${_extracted})
    file(TOUCH_NOCREATE "${_f}")
endforeach()

# Defense-in-depth: make the extracted masters read-only (version-gated;
# the structural guarantee in DiscPaths is the real protection).
if(READ_ONLY AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.19)
    file(GLOB _extracted "${OUTDIR}/*.dat" "${OUTDIR}/*.dsc")
    foreach(_f ${_extracted})
        file(CHMOD "${_f}" PERMISSIONS OWNER_READ GROUP_READ WORLD_READ)
    endforeach()
endif()
