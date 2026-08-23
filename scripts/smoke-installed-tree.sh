#!/usr/bin/env bash
#
# smoke-installed-tree.sh - validate an installed/extracted Beebium bundle is
# runnable on THIS machine. Unlike the in-tree "install_smoke" CTest, this
# operates on an already-installed <prefix> with no build tree present, so it is
# the check used to prove a release tarball runs on a foreign distro (e.g. an
# Arch container that never saw the build).
#
# It verifies the three things that distinguish a self-contained bundle:
#   1. the bundle is self-contained -- the server does NOT dynamically link
#      gRPC/protobuf/abseil/etc. (only the base system libraries);
#   2. the extension ABI shared libraries resolve via RPATH and the plugin tree
#      is discovered (`list-extensions`);
#   3. ROMs resolve through the share fallback and the gRPC server boots.
#
# Usage: smoke-installed-tree.sh <prefix>      (prefix = the .../opt/beebium dir)

set -euo pipefail

prefix="${1:?usage: smoke-installed-tree.sh <prefix>}"
# Canonicalise to an absolute path. The bare-command test below symlinks the
# server binary into a temp dir; with a relative prefix that symlink would dangle
# (it resolves relative to the symlink's own location, not the caller's cwd).
prefix="$(cd "${prefix}" && pwd)"
server="${prefix}/bin/beebium-model-b"

fail() { echo "SMOKE FAIL: $*" >&2; exit 1; }

[ -x "${server}" ] || fail "server binary missing or not executable: ${server}"

echo "== uname: $(uname -m) / $(uname -s) =="
if command -v ldd >/dev/null 2>&1; then
    echo "== ldd ${server##*/} (self-containment check) =="
    ldd_out="$(ldd "${server}" || true)"
    echo "${ldd_out}"
    # A self-contained bundle must NOT pull these in dynamically -- they are
    # statically linked from vcpkg. If any appears, the bundle is not portable.
    if echo "${ldd_out}" | grep -Eiq 'libgrpc|libprotobuf|libabsl|libre2|libcares|libssl|libcrypto|libupb'; then
        fail "bundle dynamically links a vendored dependency (not self-contained):
$(echo "${ldd_out}" | grep -Ei 'libgrpc|libprotobuf|libabsl|libre2|libcares|libssl|libcrypto|libupb')"
    fi
fi

echo "== list-extensions (ABI lib load via RPATH + plugin discovery) =="
ext_out="$("${server}" list-extensions)" || fail "list-extensions exited non-zero"
echo "${ext_out}"
# One plugin of each kind plus the network-serial extensions (rpc-serial pulls in
# gRPC, so its presence also proves those deps resolve from the installed tree).
expected_plugins="scsi-hdd acorn-rtc piconet ip232-serial rpc-serial loopback-serial rfc2217-client-serial rfc2217-server-serial"
for plugin in ${expected_plugins}; do
    echo "${ext_out}" | grep -q "${plugin}" \
        || fail "plugin '${plugin}' not discovered from the installed tree"
done

# Bare-command invocation through a PATH symlink: the shell passes only the
# command name as argv[0] (as the .deb's /usr/bin/beebium-model-b symlink does),
# so extension discovery must resolve the executable from the OS rather than
# canonicalising argv[0]. Guards the regression where plugins silently vanish
# under the package's primary entrypoint.
echo "== bare-command discovery (PATH symlink, argv0 = command name) =="
linkdir="$(mktemp -d)"
ln -s "${server}" "${linkdir}/beebium-model-b"
bare_out="$(PATH="${linkdir}:${PATH}" beebium-model-b list-extensions)" \
    || fail "bare-command list-extensions exited non-zero"
for plugin in ${expected_plugins}; do
    echo "${bare_out}" | grep -q "${plugin}" \
        || fail "plugin '${plugin}' not discovered under bare-command invocation -- argv[0]-based discovery has regressed"
done
rm -rf "${linkdir}"

echo "== boot (ROM discovery via share fallback + gRPC bring-up) =="
boot_log="$(mktemp)"
"${server}" start \
    --mos acorn-mos_1_20.rom \
    --sideways 15:rom:bbc-basic_2.rom \
    --port 0 >"${boot_log}" 2>&1 &
pid=$!
for _ in $(seq 1 40); do
    grep -q "Listening on port" "${boot_log}" && break
    kill -0 "${pid}" 2>/dev/null || break
    sleep 0.5
done
kill "${pid}" 2>/dev/null || true
wait "${pid}" 2>/dev/null || true
if ! grep -q "Listening on port" "${boot_log}"; then
    echo "---- server output ----" >&2
    cat "${boot_log}" >&2
    fail "server did not reach 'Listening on port'"
fi
grep "Listening on port" "${boot_log}"

echo "== presets shipped (share/beebium/presets non-empty) =="
preset_count="$(find "${prefix}/share/beebium/presets" -maxdepth 1 -name '*.preset.beebium' 2>/dev/null | wc -l | tr -d ' ')"
[ "${preset_count}" -gt 0 ] \
    || fail "no *.preset.beebium under ${prefix}/share/beebium/presets -- presets drive --preset machine configs"
echo "found ${preset_count} preset(s)"

echo "SMOKE PASS: ${prefix} is runnable on $(uname -m)/$(uname -s)"
