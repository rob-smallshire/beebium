#!/usr/bin/env bash
#
# verify-server-wheel.sh - install a beebium-server wheel and the beebium client
# wheel into a fresh venv and prove the client finds and drives that server.
#
# The release runs it on each server wheel, on a runner of the wheel's own
# platform, before anything is published; CI runs it on master against a
# macOS arm64 wheel made from that run's build, so a break in the wheel's
# layout shows on master rather than at a tag. Both run the same checks:
#
#   1. the server package imports and names its executable;
#   2. smoke-installed-tree.sh --layout wheel passes on the wheel's _bundle;
#   3. with no BEEBIUM_SERVER and no checkout build in sight, the client's
#      ServerInstallation.default() chooses the wheel;
#   4. packaging/smoke/test_smoke.py: the client launches the wheel's server;
#   5. the README snippets of the beebium-server and beebium client READMEs run
#      against it (the client's include choose_server, which needs a variant
#      other than the Model B, so every wheel exercises it).
#
# Usage: verify-server-wheel.sh <server-wheel-filepath> <client-wheel-filepath>
#
# Needs uv. The venv's interpreter is uv's choice, so set UV_PYTHON to pin it.
# The venv and the directory the resolver is run from live in a temporary
# directory that is removed on exit.

set -euo pipefail

usage="usage: verify-server-wheel.sh <server-wheel-filepath> <client-wheel-filepath>"
server_wheel_filepath="${1:?${usage}}"
client_wheel_filepath="${2:?${usage}}"
[ -f "${server_wheel_filepath}" ] || { echo "no such server wheel: ${server_wheel_filepath}" >&2; exit 2; }
[ -f "${client_wheel_filepath}" ] || { echo "no such client wheel: ${client_wheel_filepath}" >&2; exit 2; }

repo_dirpath="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
echo "server wheel: ${server_wheel_filepath}"
echo "client wheel: ${client_wheel_filepath}"

# Under RUNNER_TEMP in CI, which the runner empties per job; /tmp persists on a
# self-hosted runner, so the trap removes the directory either way.
work_dirpath="$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/verify-server-wheel.XXXXXX")"
trap 'rm -rf "${work_dirpath}"' EXIT

venv_dirpath="${work_dirpath}/venv"
uv venv "${venv_dirpath}"
python_filepath="${venv_dirpath}/bin/python"
uv pip install --python "${python_filepath}" \
    "${server_wheel_filepath}" "${client_wheel_filepath}" pytest pytest-timeout

echo "== import + paths =="
"${python_filepath}" -c "import beebium.server as s; print(s.__version__, s.executable_filepath())"

echo "== smoke-installed-tree.sh against the wheel bundle =="
package_dirpath="$("${python_filepath}" -c 'import beebium.server, os; print(os.path.dirname(beebium.server.__file__))')"
bash "${repo_dirpath}/scripts/smoke-installed-tree.sh" --layout wheel "${package_dirpath}/_bundle"

echo "== resolver must choose the wheel (no env, no checkout build, outside the repo) =="
outside_dirpath="${work_dirpath}/outside"
mkdir -p "${outside_dirpath}"
( cd "${outside_dirpath}" && env -u BEEBIUM_SERVER "${python_filepath}" -c "
from beebium.client import ServerInstallation
i = ServerInstallation.default()
print('resolved origin:', i.origin)
assert i.origin == 'beebium-server wheel', i.origin
" )

echo "== full smoke: client launches the wheel server with no arguments =="
env -u BEEBIUM_SERVER "${python_filepath}" -m pytest "${repo_dirpath}/packaging/smoke/test_smoke.py" -q

echo "== README snippets: the beebium-server README examples run against this wheel =="
env -u BEEBIUM_SERVER "${python_filepath}" -m pytest "${repo_dirpath}/packaging/python-server/readme/snippets" -q

echo "== README snippets: the client README examples run against this wheel =="
# The full beebium-server wheel ships every variant, so this covers the
# choose_server snippet (model-b-plus), which the everyday integration legs
# skip because their artifact ships only beebium-model-b. Wheel origin passes
# the snippet guard with BEEBIUM_SERVER unset.
env -u BEEBIUM_SERVER "${python_filepath}" -m pytest "${repo_dirpath}/clients/beebium-python-client/readme/snippets" -q

echo "VERIFIED: $(basename "${server_wheel_filepath}") with $(basename "${client_wheel_filepath}")"
