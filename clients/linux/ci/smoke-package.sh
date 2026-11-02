#!/bin/sh
# Install the client .deb with apt so its Depends resolve, run it, validate
# its desktop entry and metainfo, and remove it, leaving nothing in /usr.
# Run as root in a clean container of the distribution it was built for.
# Usage: clients/linux/ci/smoke-package.sh path/to/beebium-client_V_ARCH.deb
set -eu
export DEBIAN_FRONTEND=noninteractive
deb=$(realpath "$1")
apt-get update
apt-get install -y "$deb"
QT_QPA_PLATFORM=offscreen beebium --help
apt-get install -y --no-install-recommends desktop-file-utils appstream
desktop-file-validate /usr/share/applications/org.beebium.Beebium.desktop
appstreamcli validate --no-net /usr/share/metainfo/org.beebium.Beebium.metainfo.xml
grep -q "x-beebium-machine" /usr/share/mime/globs2
apt-get remove -y beebium-client
left=$(find /usr \( -name "*beebium*" -o -name "org.beebium*" \) -not -path "/usr/share/doc/*" | wc -l)
if [ "$left" -ne 0 ]; then
    echo "::error::removing beebium-client left $left files in /usr"
    exit 1
fi
echo "beebium-client installs, runs, validates and removes cleanly"
