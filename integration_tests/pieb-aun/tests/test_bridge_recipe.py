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

"""The published PiEconetBridge recipe, run against a bridge started outside the test (#143).

This is the configuration a user follows from docs/networking.md ("Connecting
to a PiEconetBridge"), run as written: the bridge exposes a fileserver on 1.254
and maps Beebium as station 2.80; Beebium declares ``--aun net=2``, is station
80, and maps 1.254 to the bridge with ``--aun map=``. The guest logs in with
``*I AM 1.254 SYST`` and catalogues the fileserver's disc.

Unlike test_login.py, the bridge is not started here. The opt-in workflow
(.github/workflows/pieb-bridge-recipe.yml), or a developer, starts it from
docker/pieconetbridge/run-recipe.sh -- or natively from the same rendered
config -- and tells the test where it is:

    BEEBIUM_PIEB_RECIPE_BRIDGE    host:port of the bridge's AUN listener,
                                  e.g. 127.0.0.1:32768
    BEEBIUM_PIEB_RECIPE_AUN_PORT  the UDP port the bridge's ``AUN MAP HOST
                                  2.80`` line points at; Beebium binds it

Without them the tests skip. The bridge must reach Beebium without NAT (host
networking, or a native bridge), because its static map entry matches our
datagrams by source address and port.

Every wait is in emulated time: the guest's own timeouts, NFS's "No reply"
among them, are emulated, so a slow host makes the test slower, never wrong.
"""

from __future__ import annotations

import os

import pytest

from beebium.client import Beebium

# The recipe's addressing, as the bridge's configuration states it.
FILESERVER = "1.254"
BEEBIUM_NET = 2
BEEBIUM_STATION = 80

# Emulated-time budgets. Generous, since a slow host only costs wall time.
BOOT_SECONDS = 10.0
COMMAND_SECONDS = 30.0
CHUNK_SECONDS = 0.25

# What the guest's network filing system prints when nothing answers a
# transmission: NFS 3.34 says "No reply"; ANFS 4.18, which the suite prefers,
# says "Station 1.254 not present" for the same failure.
NO_REPLY = ("No reply", f"Station {FILESERVER} not present")

# What it prints when a transaction fails, as opposed to completing.
FAILURES = (*NO_REPLY, "Not listening", "No clock", "Net error")


def _recipe_environment() -> tuple[str, int, int]:
    """The bridge's address and port, and the port Beebium must bind."""
    bridge = os.environ.get("BEEBIUM_PIEB_RECIPE_BRIDGE", "")
    aun_port = os.environ.get("BEEBIUM_PIEB_RECIPE_AUN_PORT", "")
    if not bridge or not aun_port:
        pytest.skip(
            "No bridge for the recipe test: set BEEBIUM_PIEB_RECIPE_BRIDGE=host:port "
            "and BEEBIUM_PIEB_RECIPE_AUN_PORT after starting one with "
            "docker/pieconetbridge/run-recipe.sh"
        )
    host, _, port = bridge.rpartition(":")
    return host, int(port), int(aun_port)


def _screen(bbc: Beebium) -> str:
    return bbc.video.screen_text().text


def _at_prompt(bbc: Beebium) -> bool:
    lines = [line.strip() for line in _screen(bbc).splitlines() if line.strip()]
    return bool(lines) and lines[-1] == ">"


def _run_until(bbc: Beebium, predicate, seconds: float) -> bool:
    """Run in emulated time until ``predicate`` holds or ``seconds`` pass."""
    return bbc.run_until_or_timeout(predicate, seconds, chunk_seconds=CHUNK_SECONDS)


def _failure_or_prompt(bbc: Beebium) -> bool:
    screen = _screen(bbc)
    return any(failure in screen for failure in FAILURES) or _at_prompt(bbc)


def _station_args(nfs_filepath, aun_port: int, map_entry: str | None) -> list[str]:
    aun = f"net={BEEBIUM_NET}:port={aun_port}"
    if map_entry is not None:
        aun += f":map={map_entry}"
    return [
        "--sideways", f"slot=9:type=rom:image={nfs_filepath}",
        "--station", str(BEEBIUM_STATION),
        "--aun", aun,
        "--machine-name", f"Station {BEEBIUM_STATION}",
    ]


def _boot_and_log_in(bbc: Beebium) -> str:
    """Boot to the prompt, select NFS and log in; return the screen after."""
    assert _run_until(bbc, lambda: _at_prompt(bbc), BOOT_SECONDS), \
        f"Did not reach the BASIC prompt:\n{_screen(bbc)}"
    bbc.keyboard.type("*NET\r")
    assert _run_until(bbc, lambda: _at_prompt(bbc), COMMAND_SECONDS), \
        f"*NET did not return to the prompt:\n{_screen(bbc)}"
    # The fileserver creates a privileged SYST user with a blank password when
    # its filestore starts empty, so there is no credential to set up.
    bbc.keyboard.type(f"*I AM {FILESERVER} SYST\r")
    _run_until(bbc, lambda: _failure_or_prompt(bbc), COMMAND_SECONDS)
    return _screen(bbc)


@pytest.mark.slow
@pytest.mark.timeout(600)
def test_recipe_logs_in_and_catalogues(launch_bbc, nfs_filepath) -> None:
    host, port, aun_port = _recipe_environment()
    bbc = launch_bbc(
        extra_args=_station_args(nfs_filepath, aun_port, f"{FILESERVER}@{host}@{port}"),
        startup_timeout=30.0,
    )
    screen = _boot_and_log_in(bbc)
    for failure in FAILURES:
        assert failure not in screen, f"Login failed with {failure!r}:\n{screen}"
    assert _at_prompt(bbc), f"Login did not complete:\n{screen}"

    # *CAT needs a complete exchange each way after the login, and names the
    # bridge's disc, which only the fileserver can have told us.
    bbc.keyboard.type("*CAT\r")
    assert _run_until(bbc, lambda: "BEEBIUM" in _screen(bbc) and _at_prompt(bbc),
                      COMMAND_SECONDS), \
        f"*CAT did not catalogue the bridge's disc:\n{_screen(bbc)}"


@pytest.mark.slow
@pytest.mark.timeout(600)
def test_recipe_without_the_map_entry_gets_no_reply(launch_bbc, nfs_filepath) -> None:
    """The negative control: the same station with no map entry for 1.254.

    Without the entry Beebium has nowhere to send the login, so the filing
    system gives up with its no-reply error -- which is what makes the positive
    test evidence that the map entry, and not some other route, carried the
    transaction.
    """
    _, _, aun_port = _recipe_environment()
    bbc = launch_bbc(extra_args=_station_args(nfs_filepath, aun_port, None),
                     startup_timeout=30.0)
    screen = _boot_and_log_in(bbc)
    assert any(message in screen for message in NO_REPLY), \
        f"Expected the filing system's no-reply error:\n{screen}"
