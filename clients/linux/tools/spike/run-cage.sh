#!/bin/sh
# The phase 0 spike: the client full screen in cage on the wlroots headless
# backend (no seat, no monitor), its virtual output at a chosen refresh rate,
# printing frame pacing and CPU after the run. Exits non-zero, and says so, if
# the client does not exit cleanly: a crash on closing must never pass as a
# good run. See docs/discussion/linux-client-architecture.md section 10.
#
# usage: run-cage.sh <client-filepath> <refresh-hz> <vulkan|opengl> <seconds> <host:port>
set -u
client_filepath=$1; refresh=$2; api=$3; seconds=$4; endpoint=$5
export WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=${WLR_RENDERER:-gles2}
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
unset WAYLAND_DISPLAY DISPLAY
timeout $((seconds + 30)) cage -- sh -c "
  wlr-randr --output HEADLESS-1 --custom-mode 1920x1080@${refresh}
  QT_QPA_PLATFORM=wayland '${client_filepath}' --connect '${endpoint}' --graphics-api ${api} \
    --stats-interval ${seconds} --quit-after $((seconds + 2))
  echo \"client exit status \$?\"" 2>&1 | tee /dev/stderr | grep -q "^client exit status 0$"
status=$?
[ $status -eq 0 ] || echo "FAILED: the client did not exit cleanly" >&2
exit $status
