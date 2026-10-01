#!/bin/sh
# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.
#
# Start a PiEconetBridge for the published Beebium recipe (#143): a fileserver
# on 1.254 and Beebium mapped as station 2.80.
#
# Usage:
#   run-recipe.sh render   print the rendered bridge configuration
#   run-recipe.sh start    build the image if needed and start the container
#                          in the background, printing its name
#
# Environment (the Beebium side is injected here, at run time):
#   BEEBIUM_HOST       address the bridge reaches Beebium at (default 127.0.0.1)
#   BEEBIUM_AUN_PORT   UDP port Beebium's AUN socket binds  (default 32769)
#   BRIDGE_AUN_PORT    UDP port the bridge listens on       (default 32768)
#   FILESTORE          fileserver path in the bridge's view (default /filestore)
#   CONTAINER_NAME     container name (default beebium-pieb-recipe-<pid>)
#
# The container uses host networking, so the bridge and Beebium share one
# network stack and the bridge's static AUN MAP HOST entry matches Beebium's
# datagrams exactly. That needs Linux: Docker Desktop's port forwarder
# rewrites UDP source addresses and ports, which no static map entry can
# match. On one host the two sides need different ports; the recipe's 32768
# on both sides is for a bridge and a Beebium on different hosts.
#
# A native econet-hpbridge can run the same configuration directly:
#   FILESTORE=/tmp/fs run-recipe.sh render > recipe.cfg
#   econet-hpbridge -l -c recipe.cfg -s -z -z

set -eu

HERE_DIRPATH="$(cd "$(dirname "$0")" && pwd)"
BEEBIUM_HOST="${BEEBIUM_HOST:-127.0.0.1}"
BEEBIUM_AUN_PORT="${BEEBIUM_AUN_PORT:-32769}"
BRIDGE_AUN_PORT="${BRIDGE_AUN_PORT:-32768}"
FILESTORE="${FILESTORE:-/filestore}"
IMAGE_TAG="${IMAGE_TAG:-beebium-pieb:test}"

render() {
    sed -e "s|@BEEBIUM_HOST@|${BEEBIUM_HOST}|g" \
        -e "s|@BEEBIUM_AUN_PORT@|${BEEBIUM_AUN_PORT}|g" \
        -e "s|@BRIDGE_AUN_PORT@|${BRIDGE_AUN_PORT}|g" \
        -e "s|@FILESTORE@|${FILESTORE}|g" \
        "$HERE_DIRPATH/recipe.cfg.template"
}

case "${1:-}" in
    render)
        render
        ;;
    start)
        name="${CONTAINER_NAME:-beebium-pieb-recipe-$$}"
        docker build -q -t "$IMAGE_TAG" "$HERE_DIRPATH" >/dev/null
        docker run --rm --detach --name "$name" --network host \
            --env "PIEB_CONFIG=$(render)" "$IMAGE_TAG" >/dev/null
        # The bridge logs this once every thread is up.
        tries=0
        until docker logs "$name" 2>&1 | grep -q "Main loop going to sleep."; do
            tries=$((tries + 1))
            if [ "$tries" -gt 300 ]; then
                echo "run-recipe: bridge not ready after 30 s" >&2
                docker logs "$name" >&2 || true
                exit 1
            fi
            sleep 0.1
        done
        echo "$name"
        ;;
    *)
        echo "usage: $0 render|start" >&2
        exit 64
        ;;
esac
