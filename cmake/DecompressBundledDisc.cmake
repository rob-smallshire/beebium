# Extract a bundled disc (a .tar.xz carrying the versioned .dat + .dsc) into
# the directory a system preset is put together in, which then carries it.
# Invoked via `cmake -DTARBALL=... -DOUTDIR=... -P`.

if(NOT DEFINED TARBALL OR NOT DEFINED OUTDIR)
    message(FATAL_ERROR "DecompressBundledDisc: TARBALL and OUTDIR are required")
endif()

file(MAKE_DIRECTORY "${OUTDIR}")

execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xJf "${TARBALL}"
    WORKING_DIRECTORY "${OUTDIR}"
    RESULT_VARIABLE _extract_rc)
if(NOT _extract_rc EQUAL 0)
    message(FATAL_ERROR "DecompressBundledDisc: failed to extract ${TARBALL}")
endif()
