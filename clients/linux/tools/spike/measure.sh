#!/bin/sh
# The phase 0 spike's measurements on one machine: the offscreen bench, then
# the client in cage at 50 and 60 Hz on Vulkan and OpenGL ES. Needs a server
# already running at <host:port> in MODE 1 (50 progressive frames a second).
# Fails if any run, the bench included, does not exit cleanly.
#
# usage: measure.sh <client-build-dirpath> <host:port>
set -u
build_dirpath=$1; endpoint=$2
here=$(dirname "$0")
failures=0
thermal() {
    if command -v vcgencmd >/dev/null; then
        echo "thermal: $(vcgencmd measure_temp) $(vcgencmd get_throttled)"
    fi
}
check() {
    if [ "$1" -ne 0 ]; then echo "FAILED ($2): exit status $1" >&2; failures=$((failures + 1)); fi
}
thermal
for size in 1920x1080 3840x2160; do
    QT_QPA_PLATFORM=vkkhrdisplay "$build_dirpath/beebium-display-bench" \
        --connect "$endpoint" --seconds 20 --size "$size"
    check $? "offscreen bench $size"
done
for refresh in 50 60; do
    for api in vulkan opengl; do
        echo "######## cage ${refresh} Hz ${api}"
        "$here/run-cage.sh" "$build_dirpath/beebium" "$refresh" "$api" 30 "$endpoint"
        check $? "cage ${refresh} Hz ${api}"
    done
done
thermal
[ $failures -eq 0 ] && echo "all runs exited cleanly" || echo "$failures run(s) failed" >&2
exit $failures
