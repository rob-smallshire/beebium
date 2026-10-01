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

"""Two client stations logging on to one Level 3 File Server over AUN (#149).

Three real servers on this host: the L3FS preset as station 254, and two Model
Bs, stations 80 and 81, each with a network filing system ROM. Every machine
has explicit map= entries for the others and no map file. Station 80 logs on
and catalogues, then station 81 does the same.

Each station numbers its AUN frames from the same starting handle, so 81's
first request carries a handle 80 has already used. The file server's
retransmission memory holds handles without their sender, so it acknowledges
81's request as a copy of 80's and never delivers it to the guest: 81 reports
"No reply from station 254".
"""

from __future__ import annotations

import json
import socket
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.pytest_plugin import LaunchBbc, resolve_preset

FILE_SERVER_STATION = 254
CLIENT_STATIONS = (80, 81)

#: The committed test ROMs, which include both network filing systems.
TEST_ROMS_DIRPATH = Path(__file__).parent.parent.parent.parent / "tests" / "assets" / "roms"

#: Slot for the client's network filing system ROM.
NFS_SLOT = 14

#: Emulated seconds for the file server to boot to "Starting - Ready".
FILE_SERVER_BOOT_SECONDS = 120.0

#: Emulated seconds for a client to reach the BASIC prompt after power-on.
CLIENT_BOOT_SECONDS = 10.0

#: Emulated seconds for a command to complete. Generous: NFS 3.34 takes over
#: 30 emulated seconds to give up with "No reply".
COMMAND_SECONDS = 60.0

#: Root catalogue entries of the bundled L3FS disc.
ROOT_CATALOGUE_ENTRIES = ("Library", "Passwords")


def _free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _aun_args(own_port: int, peers: dict[int, int]) -> str:
    """An --aun value with net 0, no map file, and one map= entry per peer."""
    maps = "".join(f":map=0.{station}@127.0.0.1@{port}" for station, port in peers.items())
    return f"net=0:port={own_port}:map-file=none{maps}"


def _screen_lines(bbc: Beebium) -> list[str]:
    return [line.rstrip() for line in bbc.video.screen_text().text.splitlines() if line.strip()]


def _at_prompt(bbc: Beebium) -> bool:
    lines = _screen_lines(bbc)
    return bool(lines) and lines[-1].strip() == ">"


def _run_until(bbc: Beebium, predicate, emulated_seconds: float, chunk_seconds: float = 0.25) -> bool:
    reached = bbc.run_until_or_timeout(predicate, emulated_seconds, chunk_seconds=chunk_seconds)
    bbc.debugger.ensure_running()
    return reached


def _command(bbc: Beebium, text: str) -> str:
    """Type a command, wait for the next prompt, and return the screen."""
    bbc.keyboard.type(text + "\r")
    _run_until(bbc, lambda: _at_prompt(bbc), COMMAND_SECONDS)
    return "\n".join(_screen_lines(bbc))


def _file_server_preset_without_transport(server_filepath: Path | None, scratch_dirpath: Path) -> Path:
    """The L3FS preset with its AUN transport removed.

    The transport comes from --aun instead; a preset transport and a CLI
    transport together are two transports, which a BBC machine refuses.
    """
    preset = json.loads(resolve_preset("model-b-l3fs-aun", server=server_filepath).read_text())
    del preset["econet"]["transport"]
    preset_filepath = scratch_dirpath / "l3fs-aun-no-transport.preset.beebium"
    preset_filepath.write_text(json.dumps(preset))
    return preset_filepath


@pytest.mark.parametrize("nfs_rom_filename", ["acorn-anfs_4_18.rom", "acorn-nfs_3_34.rom"])
def test_second_station_logs_on_to_file_server(
    launch_bbc: LaunchBbc,
    beebium_server_filepath: Path | None,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    nfs_rom_filename: str,
) -> None:
    nfs_rom_filepath = TEST_ROMS_DIRPATH / nfs_rom_filename
    if not nfs_rom_filepath.is_file():
        pytest.skip(f"{nfs_rom_filename} not available")

    # The file server's SCSI disc is copied on write into the work directory,
    # leaving the bundled master untouched.
    monkeypatch.setenv("BEEBIUM_DISC_WORK_DIR", str(tmp_path / "discwork"))

    ports = {station: _free_udp_port() for station in (FILE_SERVER_STATION, *CLIENT_STATIONS)}

    file_server = launch_bbc(
        preset=_file_server_preset_without_transport(beebium_server_filepath, tmp_path),
        extra_args=[
            "--aun",
            _aun_args(ports[FILE_SERVER_STATION], {s: ports[s] for s in CLIENT_STATIONS}),
        ],
        startup_timeout=30.0,
    )
    assert _run_until(
        file_server,
        lambda: "Ready" in file_server.video.screen_text().text,
        FILE_SERVER_BOOT_SECONDS,
        chunk_seconds=1.0,
    ), "file server did not reach 'Starting - Ready'"

    clients = {
        station: launch_bbc(
            extra_args=[
                "--sideways",
                f"slot={NFS_SLOT}:type=rom:image={nfs_rom_filepath}",
                "--station",
                str(station),
                "--aun",
                _aun_args(ports[station], {FILE_SERVER_STATION: ports[FILE_SERVER_STATION]}),
            ],
            startup_timeout=30.0,
        )
        for station in CLIENT_STATIONS
    }
    for station, client in clients.items():
        assert _run_until(client, lambda c=client: _at_prompt(c), CLIENT_BOOT_SECONDS), (
            f"station {station} did not reach the BASIC prompt"
        )

    # Station 80 first: this succeeds, and is what makes 81's handles stale.
    first, second = (clients[s] for s in CLIENT_STATIONS)
    _command(first, f"*I AM 0.{FILE_SERVER_STATION} SYST")
    first_catalogue = _command(first, "*.")
    assert all(entry in first_catalogue for entry in ROOT_CATALOGUE_ENTRIES), first_catalogue

    second_logon = _command(second, f"*I AM 0.{FILE_SERVER_STATION} SYST")
    assert "No reply" not in second_logon, second_logon
    second_catalogue = _command(second, "*.")
    assert all(entry in second_catalogue for entry in ROOT_CATALOGUE_ENTRIES), second_catalogue
