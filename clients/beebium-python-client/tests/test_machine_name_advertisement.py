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

"""A machine's name on the network follows its name.

A machine's name is its mDNS service instance name, so it is how anything
else -- another client's machine list, a front end deciding what to call a new
machine, a front end warning that two machines share a name -- learns what a
machine is called. A rename that never reached the network left every one of
those looking at a name the machine no longer had.
"""

from __future__ import annotations

import time
from collections.abc import Iterator
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError

pytest.importorskip("zeroconf", reason="discovery needs the zeroconf extra")

from beebium.client.discovery import MachineDiscovery  # noqa: E402

# Generous: mDNS on the local link is prompt, but a loaded CI runner is not.
PROPAGATION_TIMEOUT = 15.0


@pytest.fixture
def advertising_bbc(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
) -> Iterator[Beebium]:
    """A machine advertising itself under a known name."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=["--machine-name", "Peterhouse", "--advertise"],
            startup_timeout=20.0,
        ) as bbc:
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.fixture
def discovery() -> Iterator[MachineDiscovery]:
    browser = MachineDiscovery()
    try:
        browser.start()
    except (ImportError, OSError) as e:
        pytest.skip(f"discovery unavailable: {e}")
    try:
        yield browser
    finally:
        browser.stop()


def _wait_for_name(browser: MachineDiscovery, uuid: str, name: str) -> bool:
    """Whether the machine with this UUID is seen under this name, in time."""
    deadline = time.monotonic() + PROPAGATION_TIMEOUT
    while time.monotonic() < deadline:
        for machine in browser.machines.values():
            if machine.uuid == uuid and machine.name == name:
                return True
        time.sleep(0.2)
    return False


def _seen_names(browser: MachineDiscovery, uuid: str) -> set[str]:
    return {m.name for m in browser.machines.values() if m.uuid == uuid}


def test_a_machine_advertises_the_name_it_was_given(
    advertising_bbc: Beebium, discovery: MachineDiscovery
) -> None:
    uuid = advertising_bbc.system.identity.uuid

    assert _wait_for_name(discovery, uuid, "Peterhouse"), (
        f"never saw it advertised; saw {_seen_names(discovery, uuid)}"
    )


def test_renaming_reaches_the_network(
    advertising_bbc: Beebium, discovery: MachineDiscovery
) -> None:
    """The regression: a rename used only to change the name gRPC reported.

    An mDNS instance cannot be renamed in place, so the server has to
    withdraw the old registration and publish a new one. Without that, a
    machine renamed in a front end went on advertising its old name for as
    long as it ran.
    """
    uuid = advertising_bbc.system.identity.uuid
    assert _wait_for_name(discovery, uuid, "Peterhouse")

    advertising_bbc.system.identity.name = "Girton"

    assert _wait_for_name(discovery, uuid, "Girton"), (
        f"rename did not reach the network; saw {_seen_names(discovery, uuid)}"
    )


@pytest.mark.xfail(
    reason="MachineDiscovery can resurrect a removed service from cache (#65)",
    strict=False,
)
def test_the_old_name_does_not_linger(
    advertising_bbc: Beebium, discovery: MachineDiscovery
) -> None:
    """One machine, one name.

    Publishing the new registration without withdrawing the old would leave
    the machine visible twice, and a client avoiding names in use would go on
    avoiding a name nothing holds.

    The server does withdraw it: `dns-sd -B` across this rename shows the old
    instance removed and the new one added at the same instant, so
    mDNSResponder -- and the macOS front end browsing through it -- sees this
    correctly. What fails here is the Python browser, which can re-add a
    service from cache after having removed it (#65), so this is left as an
    expected failure rather than deleted: it is the right assertion, made
    against a browser that cannot yet keep the promise.
    """
    uuid = advertising_bbc.system.identity.uuid
    assert _wait_for_name(discovery, uuid, "Peterhouse")

    advertising_bbc.system.identity.name = "Girton"
    assert _wait_for_name(discovery, uuid, "Girton")

    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if _seen_names(discovery, uuid) == {"Girton"}:
            return
        time.sleep(0.2)

    pytest.fail(f"old name lingered; saw {_seen_names(discovery, uuid)}")
