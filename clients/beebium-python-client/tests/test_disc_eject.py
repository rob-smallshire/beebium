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

"""Safe eject against a running server.

These need a real server because what they pin is *who owns the clock*. A
pending safe eject is completed by the emulation loop; it used to be completed
by whichever client happened to be streaming disc events, which meant a server
nobody was watching would leave a disc half-ejected for ever. That failure is
invisible to a unit test of the drive and to a service test with no emulation
loop running, so it lives here.
"""

from __future__ import annotations

import time
from collections.abc import Iterator
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.disc import DiscEventType
from beebium.client.exceptions import DiscError, ServerNotFoundError

# Long enough that the eject cannot complete during a test: the motor has been
# off since the disc went in, so the usual 500ms is satisfied immediately.
NEVER_QUIESCENT_MS = 60_000


@pytest.fixture(scope="module")
def test_disc_filepath() -> Path:
    """A committed disc image; its contents do not matter here."""
    repo_root = Path(__file__).parent.parent.parent.parent
    path = repo_root / "discs" / "01-basic-validation.ssd"
    if not path.exists():
        pytest.skip(f"Test disc image not found: {path}")
    return path


@pytest.fixture
def bbc_disc(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
) -> Iterator[Beebium]:
    """A Model B with a 1770 disc controller and empty drives."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"14:rom:{dfs_1770_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _wait_until_empty(bbc: Beebium, timeout: float = 5.0) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if bbc.disc.drive(0).is_empty:
            return True
        time.sleep(0.05)
    return False


def test_safe_eject_completes_with_nobody_watching(
    bbc_disc: Beebium, test_disc_filepath: Path
) -> None:
    """The eject finishes without any client subscribed to disc events.

    This is the regression: eject progress used to be a side effect of the
    event-streaming RPC, so with no subscriber the drive stayed in EJECTING
    indefinitely.
    """
    drive = bbc_disc.disc.drive(0)
    drive.insert(test_disc_filepath)

    drive.eject()

    assert _wait_until_empty(bbc_disc), (
        f"safe eject did not complete; drive is {drive.state}"
    )


def test_safe_eject_completes_while_the_machine_is_paused(
    bbc_disc: Beebium, test_disc_filepath: Path
) -> None:
    """A paused machine still lets go of its disc.

    The drive is standing still, which is exactly when it is safe for the disc
    to leave; the emulation loop keeps its housekeeping running across the
    pause so the eject is not stranded until someone resumes.
    """
    drive = bbc_disc.disc.drive(0)
    drive.insert(test_disc_filepath)

    bbc_disc.debugger.stop()
    try:
        drive.eject()
        assert _wait_until_empty(bbc_disc), (
            f"safe eject did not complete while paused; drive is {drive.state}"
        )
    finally:
        bbc_disc.debugger.ensure_running()


def test_server_never_forces_a_pending_eject(
    bbc_disc: Beebium, test_disc_filepath: Path
) -> None:
    """The disc stays put until the caller decides otherwise.

    The server used to force after ten seconds, which is a decision about
    someone's data taken by a timer.
    """
    drive = bbc_disc.disc.drive(0)
    drive.insert(test_disc_filepath)

    drive.eject(quiescence_ms=NEVER_QUIESCENT_MS)
    time.sleep(1.0)

    assert drive.is_ejecting
    assert not drive.is_empty


def test_client_can_force_a_pending_eject(
    bbc_disc: Beebium, test_disc_filepath: Path
) -> None:
    """Ending the wait is available to whoever asked for the eject."""
    drive = bbc_disc.disc.drive(0)
    drive.insert(test_disc_filepath)
    drive.eject(quiescence_ms=NEVER_QUIESCENT_MS)
    assert drive.is_ejecting

    drive.eject(immediate=True)

    assert drive.is_empty


def test_cancel_eject_keeps_the_disc(
    bbc_disc: Beebium, test_disc_filepath: Path
) -> None:
    """Backing out of an eject leaves the disc loaded and readable."""
    drive = bbc_disc.disc.drive(0)
    drive.insert(test_disc_filepath)
    drive.eject(quiescence_ms=NEVER_QUIESCENT_MS)
    assert drive.is_ejecting

    drive.cancel_eject()

    assert drive.is_loaded
    assert drive.status.disc_url.endswith("01-basic-validation.ssd")


def test_cancel_eject_refused_when_nothing_pending(
    bbc_disc: Beebium, test_disc_filepath: Path
) -> None:
    drive = bbc_disc.disc.drive(0)
    drive.insert(test_disc_filepath)

    with pytest.raises(DiscError):
        drive.cancel_eject()


def test_eject_and_reinsert_of_one_image_is_reported(
    bbc_disc: Beebium, test_disc_filepath: Path
) -> None:
    """Both halves of a fast swap reach a subscriber.

    Events are raised where the state changes, so an excursion through EMPTY
    that begins and ends between any two observations is still reported. A
    subscriber that compared sampled state, or compared disc paths, saw
    nothing happen here at all.
    """
    drive = bbc_disc.disc.drive(0)
    drive.insert(test_disc_filepath)

    events: list[DiscEventType] = []
    handle = bbc_disc.disc.start_background_events(lambda e: events.append(e.type))
    try:
        time.sleep(0.3)
        events.clear()

        drive.eject(immediate=True)
        drive.insert(test_disc_filepath)
        time.sleep(0.5)
    finally:
        handle.stop()

    assert DiscEventType.FORCE_EJECTED in events, events
    assert DiscEventType.INSERTED in events, events
