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

"""Integration tests for the Python Sideways client.

Drive a real beebium-server (the default Model B) and assert the
Sideways wrapper maps GetSlotStatus / SubscribeEvents into the
typed Python dataclasses correctly.
"""

from __future__ import annotations

import pytest

from pathlib import Path

from beebium.client import Beebium
from beebium.client.exceptions import (
    BeebiumError,
    ServerNotFoundError,
    ServerStartupError,
)
from beebium.client.sideways import (
    ProtectionKind,
    RomHeader,
    SlotStatusReport,
    SlotType,
    SocketStatus,
)


def _group(status: SlotStatusReport, group_id: str):
    """The protection group with the given id, or None."""
    for g in status.protection_groups:
        if g.id == group_id:
            return g
    return None


@pytest.fixture
def atpl_sidewise(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
):
    """A Model B with the ATPL Sidewise board, slot 15 fitted as RAM.

    A custom BASIC is passed to exercise --language-rom: the server places it
    in this variant's default language slot (14), not slot 15 which the board
    reserves for RAM. This is the regression case for the launcher's former
    hardcoded slot-15 assumption.
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            variant="model-b-atpl-sidewise",
            extra_args=["--sideways", "slot=15:type=ram"],
        ) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_get_slot_status_returns_typed_report(bbc):
    """Default Model B preset loads BASIC; GetSlotStatus must reflect it."""
    status = bbc.sideways.get_slot_status()

    assert isinstance(status, SlotStatusReport)
    assert status.has_aliasing is True
    # Model B has four physical sockets (IC52, IC88, IC100, IC101).
    assert status.num_physical_slots == 4
    assert len(status.sockets) == 4

    for socket in status.sockets:
        assert isinstance(socket, SocketStatus)
        # Model B aliases each socket to four slots via partial decoding.
        assert len(socket.aliased_slots) == 4

    # IC101 (socket 3) holds BASIC, mirrored at slots 3, 7, 11, 15.
    ic101 = status.find_socket_for_slot(15)
    assert ic101 is not None
    assert ic101.label == "IC101"
    assert ic101.priority == 15
    assert ic101.type is SlotType.ROM
    assert ic101.populated is True

    header = ic101.rom_header
    assert isinstance(header, RomHeader)
    assert header.title == "BASIC"
    assert "language" in header.kinds


def test_empty_sockets_have_no_rom_header(bbc):
    status = bbc.sideways.get_slot_status()
    # Sockets other than IC101 are empty in the stock Model B fixture.
    # On Model B the AliasedBankedMemory keeps slot type as the default
    # (ROM) until configure_socket runs, so populated / rom_header are
    # the load-bearing "is anything here" signals - not type.
    empty = [s for s in status.sockets if s.label != "IC101"]
    assert len(empty) == 3
    for socket in empty:
        assert socket.populated is False
        assert socket.rom_header is None


def test_configure_slot_rejects_non_runtime_configurable(bbc):
    """Model B sockets are real chips - ConfigureSlot must error."""
    with pytest.raises(BeebiumError):
        bbc.sideways.configure_slot(15, SlotType.RAM)


def test_read_slot_data_returns_bytes(bbc):
    # First 16 bytes of slot 15 are BASIC's header.
    data = bbc.sideways.read_slot_data(15, offset=0, length=16)
    assert isinstance(data, bytes)
    assert len(data) == 16
    # BBC BASIC 2 starts with a CMP immediate (0xC9) - same signature
    # checked by the C++ debugger test for bank_15.
    assert data[0] == 0xC9


def test_subscribe_events_stream_is_cancellable(bbc):
    """The SubscribeEvents stream stays open with no events flowing today
    (no emitter is wired yet); make sure opening and cancelling it
    cleanly works so future tests can use it.
    """
    import threading

    error: list = []

    def reader():
        try:
            events = bbc.sideways.subscribe_events()
            # The first next() blocks until cancel/close; we never iterate
            # further. Just make sure the generator returns cleanly when
            # the underlying channel is closed by the bbc fixture's
            # teardown (via thread.join timeout).
            for _ in events:
                break
        except Exception as exc:  # noqa: BLE001 - surface anything unexpected
            error.append(exc)

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    thread.join(timeout=0.5)
    # The reader is blocked in next(); that's fine - the fixture's
    # teardown will close the channel and unblock it. The point of this
    # test is to fail loudly if subscribe_events() itself blows up.
    assert not error, f"subscribe_events raised: {error[0]!r}"


def test_atpl_sidewise_reports_16_slots_with_ram_in_15(atpl_sidewise):
    """The ATPL Sidewise exposes 16 non-aliased slots; slot 15 is RAM."""
    status = atpl_sidewise.sideways.get_slot_status()

    assert status.has_aliasing is False
    assert status.num_physical_slots == 16

    slot15 = status.find_socket_for_slot(15)
    assert slot15 is not None
    assert slot15.type is SlotType.RAM
    assert slot15.capabilities.supports_ram is True
    assert slot15.capabilities.runtime_configurable is False
    # The write-protect switch is a one-slot protection group over slot 15.
    group = _group(status, "slot-15")
    assert group is not None
    assert group.slots == (15,)
    assert group.supports_write_protect is True
    assert group.write_protected is False

    # --language-rom must place the custom BASIC in slot 14, not slot 15.
    slot14 = status.find_socket_for_slot(14)
    assert slot14 is not None
    assert slot14.type is SlotType.ROM
    assert slot14.populated is True
    assert slot14.rom_header is not None
    assert slot14.rom_header.title == "BASIC"


def test_atpl_sidewise_write_protect_roundtrip(atpl_sidewise):
    """SetSlotProtection toggles the slot-15 write group and GetSlotStatus reflects it."""
    sideways = atpl_sidewise.sideways

    assert sideways.set_protection("slot-15", ProtectionKind.WRITE_PROTECT, True) is True
    assert _group(sideways.get_slot_status(), "slot-15").write_protected is True

    assert sideways.set_protection("slot-15", ProtectionKind.WRITE_PROTECT, False) is False
    assert _group(sideways.get_slot_status(), "slot-15").write_protected is False


def test_atpl_sidewise_protection_rejects_unknown_group(atpl_sidewise):
    """The ATPL board has only the slot-15 write group; other requests are rejected."""
    with pytest.raises(BeebiumError):
        atpl_sidewise.sideways.set_protection("slot-14", ProtectionKind.WRITE_PROTECT, True)
    with pytest.raises(BeebiumError):
        atpl_sidewise.sideways.set_protection("slot-15", ProtectionKind.HIDE, True)


def test_atpl_sidewise_write_protect_at_launch(
    mos_filepath: Path,
    beebium_server_filepath: Path | None,
):
    """--write-protect engages the switch at boot, before any runtime call."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            server=beebium_server_filepath,
            variant="model-b-atpl-sidewise",
            extra_args=["--sideways", "slot=15:type=ram:write-protect"],
        ) as bbc:
            status = bbc.sideways.get_slot_status()
            slot15 = status.find_socket_for_slot(15)
            assert slot15 is not None
            assert slot15.type is SlotType.RAM
            assert _group(status, "slot-15").write_protected is True
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_atpl_sidewise_write_protect_launch_rejects_non_ram_slot(
    mos_filepath: Path,
    beebium_server_filepath: Path | None,
):
    """--write-protect on a slot that cannot hold RAM fails at launch."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            server=beebium_server_filepath,
            variant="model-b-atpl-sidewise",
            # write-protect on a non-RAM slot is rejected at parse time
            extra_args=["--sideways", "slot=14:type=rom:image=bbc-basic_2.rom:write-protect"],
        ):
            pass
    except ServerNotFoundError as e:
        pytest.skip(str(e))
    except ServerStartupError:
        return  # expected: server rejects the configuration
    pytest.fail("expected ServerStartupError for --write-protect on a ROM-only slot")


def test_watford_launch_protection_flags(
    mos_filepath: Path,
    beebium_server_filepath: Path | None,
):
    """--write-protect <group> and --hide <group> engage board switches at launch."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            server=beebium_server_filepath,
            variant="model-b-watford-rom-ram",
            extra_args=[
                "--sideways", "slot=0:type=ram",
                "--sideways", "slot=14:type=ram",
                "--write-protect", "board",
                "--hide", "slot-14",
            ],
        ) as bbc:
            status = bbc.sideways.get_slot_status()

            board = _group(status, "board")
            assert board is not None
            assert board.supports_write_protect is True
            assert board.write_protected is True

            slot14 = _group(status, "slot-14")
            assert slot14 is not None
            assert slot14.supports_hide is True
            assert slot14.hidden is True
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_watford_launch_protection_rejects_unknown_group(
    mos_filepath: Path,
    beebium_server_filepath: Path | None,
):
    """--write-protect on a group the machine does not have fails at launch."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            server=beebium_server_filepath,
            variant="model-b-watford-rom-ram",
            extra_args=["--write-protect", "no-such-group"],
        ):
            pass
    except ServerNotFoundError as e:
        pytest.skip(str(e))
    except ServerStartupError:
        return  # expected: server rejects the unknown protection group
    pytest.fail("expected ServerStartupError for --write-protect on an unknown group")
