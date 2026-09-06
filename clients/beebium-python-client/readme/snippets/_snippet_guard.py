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

"""Server-origin guard for the README snippets (shared by conftest and its test).

A uniquely named module (not conftest) so `import _snippet_guard` is
unambiguous even when the snippets run alongside tests/ (which has its own
conftest.py). Not a rendered snippet.
"""

from beebium.client.exceptions import ServerNotFoundError
from beebium.client.installation import ServerInstallation

# Server origins the README examples are actually about: the version-locked
# wheel, an explicit BEEBIUM_SERVER, or a build in the surrounding checkout.
# A server merely found on PATH (e.g. a Homebrew install that predates this
# client, or one built from a different protocol) is NOT what the examples
# document -- running against it is the stray-server trap -- so it is refused.
ACCEPTED_ORIGINS = frozenset({"beebium-server wheel", "BEEBIUM_SERVER", "checkout build"})


def skip_reason_for_origin(origin: str, description: str) -> str | None:
    """Why the README snippets should skip for a resolved server, or None to run.

    Pure decision logic (no environment access) so it can be unit-tested: the
    snippets run only against a server the examples are about; any other origin
    -- notably PATH -- is refused, naming what it refused.
    """
    if origin in ACCEPTED_ORIGINS:
        return None
    return (
        f"README snippets need the beebium-server wheel, BEEBIUM_SERVER, or a "
        f"checkout build; refusing the server resolved from {origin} "
        f"({description}). A server merely on PATH is not what the examples document."
    )


def snippet_skip_reason() -> str | None:
    """Resolve a server and decide whether the README snippets may run."""
    try:
        installation = ServerInstallation.default()
    except ServerNotFoundError as exc:
        return f"no beebium-server available for README snippets: {exc}"
    return skip_reason_for_origin(installation.origin, installation.describe())


def variant_skip_reason(variant: str) -> str | None:
    """Why a snippet needing `variant` should skip, or None if it is available.

    A snippet that launches a non-default variant (e.g. model-b-plus) can only
    run against an installation that ships that binary. The CI integration
    artifact ships only beebium-model-b, so the sibling-variant lookup rightly
    fails there -- skip with a precise reason instead. The full beebium-server
    wheel (all four variants), used on the verify-server-wheels legs, runs it
    for real.
    """
    try:
        installation = ServerInstallation.default()
    except ServerNotFoundError as exc:
        return f"no beebium-server available for README snippets: {exc}"
    available = installation.variants()
    if variant not in available:
        return (
            f"server ({installation.describe()}) does not ship the '{variant}' "
            f"variant this snippet launches; it has {', '.join(available) or 'none'}."
        )
    return None
