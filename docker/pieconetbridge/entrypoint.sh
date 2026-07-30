#!/bin/sh
# Render a PiEconetBridge configuration and run the bridge in the foreground.
#
# The whole configuration is supplied by the caller in PIEB_CONFIG, so the
# topology under test lives in the test fixture rather than being smeared
# across this script and a pile of environment variables. The container stays
# a dumb runner of whatever config it is handed.
#
# Environment:
#   PIEB_CONFIG     (required) the bridge configuration file, verbatim
#   PIEB_DISCS      (optional) space-separated filestore disc directories to
#                   create, in upstream's "<disc-number><NAME>" form.
#                   Default: "0BEEBIUM"
#   PIEB_DEBUG      (optional) any non-empty value adds -s (dump config at
#                   startup) and raises the debug level
#
# The fileserver initialises its own Passwords file with a privileged SYST
# user (blank password) when the filestore has none, so a bare empty directory
# is a valid starting point.

set -eu

if [ -z "${PIEB_CONFIG:-}" ]; then
    echo "entrypoint: PIEB_CONFIG is not set -- nothing to run" >&2
    exit 64
fi

printf '%s\n' "$PIEB_CONFIG" > /etc/econet-gpio/econet-hpbridge.cfg

for disc in ${PIEB_DISCS:-0BEEBIUM}; do
    mkdir -p "/filestore/${disc}"
done

echo "entrypoint: configuration:" >&2
sed 's/^/entrypoint:   /' /etc/econet-gpio/econet-hpbridge.cfg >&2

# -l  Don't try to open Econet devices: IP-only operation. This is upstream's
#     own switch for running without the hardware, and it makes the intent
#     explicit rather than relying on the absence of a WIRE NET line.
# -s  Dump the resolved topology at startup. Always on: it is the cheapest
#     way to see how the bridge actually understood the config, which is the
#     first thing anyone wants when a test fails.
# -z  Debug level. Two are always on, for two reasons: the startup progress
#     lines the test fixture waits on to decide the bridge is ready are only
#     emitted at this level, and a failed interop test is close to
#     undiagnosable without the bridge's own account of what it saw.
set -- -l -c /etc/econet-gpio/econet-hpbridge.cfg -s -z -z

if [ -n "${PIEB_DEBUG:-}" ]; then
    set -- "$@" -z -z
fi

echo "entrypoint: exec econet-hpbridge $*" >&2
exec /usr/local/sbin/econet-hpbridge "$@"
