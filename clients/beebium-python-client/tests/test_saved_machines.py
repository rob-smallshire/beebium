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

"""Saved machines on the command line (#103): create-machine makes one from a
preset, describe-machine says what it is and who runs it, start --machine runs
it, keeping its identity and its media from one run to the next, and
reset-battery forgets its battery-backed state.

The Integra-B's battery-backed state across runs is tested with the board, in
test_integra_b.py.
"""

from __future__ import annotations

import json
import re
import socket
import subprocess
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.installation import ServerInstallation

EX_CANTCREAT = 73
EX_TEMPFAIL = 75


@pytest.fixture
def installation(beebium_server_filepath: Path | None) -> ServerInstallation:
    try:
        if beebium_server_filepath is None:
            return ServerInstallation.default()
        return ServerInstallation.coerce(beebium_server_filepath)
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.fixture
def run(installation: ServerInstallation):
    """Run a server subcommand of a variant, returning the completed process."""

    def _run(*args: str | Path, variant: str = "model-b") -> subprocess.CompletedProcess[str]:
        try:
            executable_filepath = installation.executable_filepath(variant)
        except ServerNotFoundError as e:
            pytest.skip(str(e))
        return subprocess.run(
            [str(executable_filepath), *map(str, args)],
            capture_output=True,
            text=True,
            timeout=60,
        )

    return _run


@pytest.fixture
def describe(run):
    def _describe(machine: Path) -> dict:
        result = run("describe-machine", machine, "--format", "jsonl")
        assert result.returncode == 0, result.stderr
        return json.loads(result.stdout)

    return _describe


def _launch(installation: ServerInstallation, machine: Path, *extra_args: str,
            startup_timeout: float = 10.0):
    return Beebium.launch(
        server=installation,
        extra_args=["--machine", str(machine), *extra_args],
        startup_timeout=startup_timeout,
    )


# ---------------------------------------------------------------------------
# The command line
# ---------------------------------------------------------------------------


def test_create_machine_makes_a_described_machine_directory(run, describe, tmp_path: Path):
    machine = tmp_path / "Disc Machine.beebium-machine"
    result = run("create-machine", "--preset", "model-b-disc", "--output", machine)
    assert result.returncode == 0, result.stderr
    assert Path(result.stdout.strip()) == machine

    description = describe(machine)
    assert description["machine_dirpath"] == str(machine)
    assert description["model"] == "model-b"
    assert description["preset"] == "model-b-disc"
    assert re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ", description["created"])
    assert "last_used" not in description  # never run
    assert "locked_by_pid" not in description  # not running

    # The document file names the machine by its uuid; describing the
    # document describes its directory.
    document = json.loads((machine / "Disc Machine.beebium-machine").read_text())
    assert document == {"format": "beebium-machine", "version": 1,
                        "uuid": description["uuid"]}
    assert describe(machine / "Disc Machine.beebium-machine") == description


def test_create_machine_never_writes_into_an_existing_directory(run, tmp_path: Path):
    result = run("create-machine", "--output", tmp_path)
    assert result.returncode == EX_CANTCREAT
    assert "already exists" in result.stderr


def test_describe_machine_refuses_a_directory_that_is_not_a_machine(run, tmp_path: Path):
    result = run("describe-machine", tmp_path)
    assert result.returncode != 0
    assert "is not a saved machine" in result.stderr


def test_a_machine_keeps_its_identity_on_every_run(installation, run, describe,
                                                    tmp_path: Path):
    machine = tmp_path / "Kept.beebium-machine"
    assert run("create-machine", "--machine-name", "Kept {model}",
               "--output", machine).returncode == 0
    uuid = describe(machine)["uuid"]

    for _ in range(2):
        with _launch(installation, machine) as bbc:
            identity = bbc.system.identity
            assert identity.uuid == uuid
            assert identity.name_template == "Kept {model}"
        assert describe(machine)["uuid"] == uuid
    assert "last_used" in describe(machine)


def test_a_running_machine_is_not_opened_twice(installation, run, describe, tmp_path: Path):
    machine = tmp_path / "Busy.beebium-machine"
    assert run("create-machine", "--output", machine).returncode == 0

    with _launch(installation, machine):
        holder = describe(machine)["locked_by_pid"]
        assert isinstance(holder, int)

        second = run("start", "--machine", machine, "--port", "0", "--wait=api")
        assert second.returncode == EX_TEMPFAIL
        assert f"{machine} is in use by process {holder}" in second.stderr

        reset = run("reset-battery", machine)
        assert reset.returncode == EX_TEMPFAIL
        assert f"is in use by process {holder}" in reset.stderr

    assert "locked_by_pid" not in describe(machine)


def test_a_saved_machine_runs_only_on_its_own_model(installation, run, tmp_path: Path):
    machine = tmp_path / "B.beebium-machine"
    assert run("create-machine", "--output", machine).returncode == 0
    result = run("start", "--machine", machine, "--port", "0", "--wait=api",
                 variant="model-b-plus")
    assert result.returncode != 0
    assert "is a 'model-b' machine" in result.stderr


def test_a_saved_machine_keeps_its_own_identity(run, tmp_path: Path):
    machine = tmp_path / "Own.beebium-machine"
    assert run("create-machine", "--output", machine).returncode == 0
    result = run("start", "--machine", machine, "--port", "0", "--wait=api",
                 "--machine-uuid", "12345678-1234-1234-1234-123456789abc")
    assert result.returncode != 0
    assert "cannot change a saved machine's identity" in result.stderr


# ---------------------------------------------------------------------------
# A saved Level 3 File Server keeps its disc (the regression #103 answers)
# ---------------------------------------------------------------------------

FILE_SERVER_STATION = 254
CLIENT_STATION = 80
TEST_ROMS_DIRPATH = Path(__file__).parent.parent.parent.parent / "tests" / "assets" / "roms"
NFS_ROM_FILEPATH = TEST_ROMS_DIRPATH / "acorn-anfs_4_18.rom"
FILE_SERVER_BOOT_SECONDS = 120.0
CLIENT_BOOT_SECONDS = 10.0
COMMAND_SECONDS = 60.0


def _free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _aun_args(own_port: int, peer_station: int, peer_port: int) -> str:
    """Net 0, no map file, no mDNS: the one peer is found only by its map=."""
    return (f"net=0:port={own_port}:map-file=none:discovery=off"
            f":map=0.{peer_station}@127.0.0.1@{peer_port}")


def _screen(bbc: Beebium) -> str:
    return bbc.video.screen_text().text


def _at_prompt(bbc: Beebium) -> bool:
    lines = [line.rstrip() for line in _screen(bbc).splitlines() if line.strip()]
    return bool(lines) and lines[-1].strip() == ">"


def _run_until(bbc: Beebium, predicate, emulated_seconds: float,
               chunk_seconds: float = 0.25) -> bool:
    reached = bbc.run_until_or_timeout(predicate, emulated_seconds, chunk_seconds=chunk_seconds)
    bbc.debugger.ensure_running()
    return reached


def _command(bbc: Beebium, text: str) -> str:
    bbc.keyboard.type(text + "\r")
    assert _run_until(bbc, lambda: _at_prompt(bbc), COMMAND_SECONDS), _screen(bbc)
    return _screen(bbc)


def _with_file_server(installation, machine: Path, mos_filepath: Path, basic_filepath,
                      session) -> None:
    """Run the saved file server and a client station logged on to it, give
    the client to `session`, then shut both down gracefully."""
    ports = {FILE_SERVER_STATION: _free_udp_port(), CLIENT_STATION: _free_udp_port()}
    with _launch(installation, machine, "--aun",
                 _aun_args(ports[FILE_SERVER_STATION], CLIENT_STATION, ports[CLIENT_STATION]),
                 startup_timeout=30.0) as file_server:
        assert _run_until(file_server, lambda: "Ready" in _screen(file_server),
                          FILE_SERVER_BOOT_SECONDS, chunk_seconds=1.0), _screen(file_server)
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=installation,
            extra_args=[
                "--sideways", f"slot=14:type=rom:image={NFS_ROM_FILEPATH}",
                "--station", str(CLIENT_STATION),
                "--aun", _aun_args(ports[CLIENT_STATION], FILE_SERVER_STATION,
                                   ports[FILE_SERVER_STATION]),
            ],
            startup_timeout=30.0,
        ) as client:
            assert _run_until(client, lambda: _at_prompt(client), CLIENT_BOOT_SECONDS)
            logon = _command(client, f"*I AM 0.{FILE_SERVER_STATION} SYST")
            assert "No reply" not in logon, logon
            session(client)
            _command(client, "*BYE")


def test_a_saved_file_server_keeps_a_file_across_runs(installation, run, tmp_path: Path,
                                                       mos_filepath: Path,
                                                       basic_filepath: Path | None):
    if not NFS_ROM_FILEPATH.is_file():
        pytest.skip(f"{NFS_ROM_FILEPATH.name} not available")
    machine = tmp_path / "File Server.beebium-machine"
    created = run("create-machine", "--preset", "model-b-l3fs-aun", "--output", machine)
    if created.returncode != 0 and "model-b-l3fs-aun" in created.stderr:
        pytest.skip(created.stderr.strip())
    assert created.returncode == 0, created.stderr

    def save_a_file(client: Beebium) -> None:
        _command(client, "*SAVE KEPT 3000 +100")
        assert "KEPT" in _command(client, "*.")

    def find_the_file(client: Beebium) -> None:
        assert "KEPT" in _command(client, "*.")

    def miss_the_file(client: Beebium) -> None:
        catalogue = _command(client, "*.")
        assert "Library" in catalogue and "KEPT" not in catalogue, catalogue

    _with_file_server(installation, machine, mos_filepath, basic_filepath, save_a_file)
    _with_file_server(installation, machine, mos_filepath, basic_filepath, find_the_file)

    # The file went to the machine's own disc, never to the preset's: another
    # machine made from it starts without it.
    other = tmp_path / "Other.beebium-machine"
    assert run("create-machine", "--preset", "model-b-l3fs-aun", "--output", other).returncode == 0
    _with_file_server(installation, other, mos_filepath, basic_filepath, miss_the_file)
