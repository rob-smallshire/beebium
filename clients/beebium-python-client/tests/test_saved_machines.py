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
import os
import re
import shutil
import socket
import subprocess
import sys
from collections.abc import Callable
from pathlib import Path
from urllib.parse import quote

import grpc
import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.installation import ServerInstallation
from beebium.client.system import MachineIdentity, ServerStatus
from beebium.ext.peripheral.acorn_rtc import AcornRtc

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
    assert description["name"] == "Disc Machine"  # its document's name
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
    assert run("create-machine", "--machine-description", "Kept {machine-model}",
               "--output", machine).returncode == 0
    uuid = describe(machine)["uuid"]

    for _ in range(2):
        with _launch(installation, machine) as bbc:
            identity = bbc.system.identity
            assert identity.uuid == uuid
            assert identity.description_template == "Kept {machine-model}"
            assert identity.description == "Kept BBC Model B"
            assert identity.name == "Kept"  # known by its document's name
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
# Saving a running machine (SystemService.SaveMachine)
# ---------------------------------------------------------------------------


def _run_for(bbc: Beebium, emulated_seconds: float) -> None:
    """Run long enough for the machine to have shown a screen."""
    bbc.run_until_or_timeout(lambda: False, emulated_seconds, chunk_seconds=emulated_seconds)
    bbc.debugger.ensure_running()


def _template_media_written(bbc: Beebium) -> bool:
    events = bbc.system.watch_status()
    try:
        return next(events).template_media_written
    finally:
        events.close()


def test_a_machine_not_yet_saved_is_saved_where_it_is_told(installation, run, describe,
                                                            tmp_path: Path):
    machine = tmp_path / "First.beebium-machine"
    with Beebium.launch(server=installation) as bbc:
        assert not bbc.system.machine_saved
        transient = Path(bbc.system.machine_dirpath)
        assert transient.is_dir()
        uuid = bbc.system.identity.uuid
        _run_for(bbc, 0.5)

        with pytest.raises(grpc.RpcError) as no_destination:
            bbc.system.save_machine()
        assert no_destination.value.code() == grpc.StatusCode.INVALID_ARGUMENT
        with pytest.raises(grpc.RpcError) as existing:
            bbc.system.save_machine(tmp_path)
        assert existing.value.code() == grpc.StatusCode.ALREADY_EXISTS

        saved = bbc.system.save_machine(machine)
        assert saved.machine_dirpath == str(machine)
        assert saved.identity.uuid == uuid  # the same machine, now saved
        assert bbc.system.machine_saved
        assert bbc.system.machine_dirpath == str(machine)
        assert not transient.exists()

        description = describe(machine)
        assert description["uuid"] == uuid
        assert isinstance(description["locked_by_pid"], int)
        assert Path(description["thumbnail_filepath"]).is_file()
        assert (machine / "preset.json").is_file()
        assert (machine / "First.beebium-machine").is_file()

    assert "locked_by_pid" not in describe(machine)
    with _launch(installation, machine) as bbc:
        assert bbc.system.identity.uuid == uuid
        assert bbc.system.machine_saved


def test_save_as_makes_another_machine_and_runs_on_from_it(installation, run, describe,
                                                           tmp_path: Path):
    original = tmp_path / "Original.beebium-machine"
    copy = tmp_path / "Copy.beebium-machine"
    assert run("create-machine", "--output", original).returncode == 0
    original_uuid = describe(original)["uuid"]

    with _launch(installation, original) as bbc:
        _run_for(bbc, 0.5)
        assert bbc.system.identity.name == "Original"
        events = bbc.system.watch_status()
        assert next(events).status == ServerStatus.READY
        saved = bbc.system.save_machine(copy)
        assert saved.machine_dirpath == str(copy)
        assert saved.identity.uuid != original_uuid
        # The fork is known by its own document's name, announced at once.
        assert saved.identity.name == "Copy"
        changed = next(e for e in events if e.status == ServerStatus.IDENTITY_CHANGED)
        events.close()
        assert changed.identity.name == "Copy"
        assert changed.identity.uuid == saved.identity.uuid
        assert bbc.system.identity.uuid == saved.identity.uuid
        assert bbc.system.machine_dirpath == str(copy)

        # The original is left as it was, and free.
        assert describe(original)["uuid"] == original_uuid
        assert "locked_by_pid" not in describe(original)
        assert describe(copy)["uuid"] == saved.identity.uuid
        assert isinstance(describe(copy)["locked_by_pid"], int)

        # Saved in place from now on.
        assert bbc.system.save_machine().machine_dirpath == str(copy)


def test_a_save_replaces_a_saved_machine_only_when_asked_and_only_one_it_may(
        installation, run, describe, tmp_path: Path):
    running = tmp_path / "Running.beebium-machine"
    old = tmp_path / "Old.beebium-machine"
    busy = tmp_path / "Busy.beebium-machine"
    not_a_machine = tmp_path / "Folder"
    not_a_machine.mkdir()
    for machine in (running, old, busy):
        assert run("create-machine", "--output", machine).returncode == 0
    (old / "notes.txt").write_text("the old machine's")
    old_uuid = describe(old)["uuid"]

    with _launch(installation, busy), _launch(installation, running) as bbc:
        _run_for(bbc, 0.5)

        def refused(destination: Path, **kwargs) -> grpc.RpcError:
            with pytest.raises(grpc.RpcError) as caught:
                bbc.system.save_machine(destination, **kwargs)
            return caught.value

        assert refused(old).code() == grpc.StatusCode.ALREADY_EXISTS
        for destination, why in ((not_a_machine, "is not a saved machine"),
                                 (running, "is this machine's own directory"),
                                 (busy, "is in use by process")):
            error = refused(destination, replace_existing=True)
            assert error.code() == grpc.StatusCode.FAILED_PRECONDITION
            assert why in error.details()
        assert describe(old)["uuid"] == old_uuid  # nothing touched

        saved = bbc.system.save_machine(old, replace_existing=True)
        assert saved.machine_dirpath == str(old)
        uuid = saved.identity.uuid
        assert uuid != old_uuid
        assert describe(old)["uuid"] == uuid
        assert not (old / "notes.txt").exists()  # the old machine is gone, whole
        assert sorted(p.name for p in tmp_path.iterdir()) == sorted(
            ["Running.beebium-machine", "Old.beebium-machine", "Busy.beebium-machine",
             "Folder"])

    with _launch(installation, old) as bbc:
        assert bbc.system.identity.uuid == uuid


def test_a_saved_machine_is_named_by_its_document(installation, run, tmp_path: Path):
    """A saved machine's name is its document's, whatever --machine-name says,
    and its description, a template, is its own."""
    machine = tmp_path / "Elite.beebium-machine"
    assert run("create-machine", "--machine-description", "On {machine-model}",
               "--output", machine).returncode == 0
    with _launch(installation, machine, "--machine-name", "Ignored") as bbc:
        identity = bbc.system.identity
        assert identity.name == "Elite"
        assert identity.description == "On BBC Model B"


def test_a_saved_machine_keeps_its_new_description_and_its_last_screen(
        installation, run, describe, tmp_path: Path):
    machine = tmp_path / "Named.beebium-machine"
    assert run("create-machine", "--output", machine).returncode == 0
    assert "description" not in describe(machine)  # none given
    assert "thumbnail_filepath" not in describe(machine)
    with _launch(installation, machine) as bbc:
        bbc.system.set_machine_description("Described {machine-model}")
        _run_for(bbc, 0.5)
    description = describe(machine)
    assert description["description"] == "Described {machine-model}"
    assert Path(description["thumbnail_filepath"]).is_file()  # kept at shutdown
    with _launch(installation, machine) as bbc:
        assert bbc.system.identity.description == "Described BBC Model B"


SUFFIX = ".beebium-machine"
REPO_ASSETS_DIRPATH = Path(__file__).parent.parent.parent.parent / "tests" / "assets"


def _move_a_running_machine(installation, run, describe, old: Path, new: Path,
                            move: Callable[[Beebium], MachineIdentity], *,
                            by_copy: bool = sys.platform == "win32") -> None:
    """Moves a running saved machine from `old` to `new` with `move`, and
    checks it there: the directory moved with its lock and document file, its
    open media are re-pointed and keep taking writes, and it reopens there
    with the same uuid. `by_copy` says whether the directory is copied rather
    than renamed, which Windows always does: a rename keeps its inode."""
    disc = REPO_ASSETS_DIRPATH / "discs" / "6502timing.ssd"
    hard_disc = REPO_ASSETS_DIRPATH / "scsi" / "scsi0.dat"
    if not disc.is_file() or not hard_disc.is_file():
        pytest.skip("test disc images not available")
    # Bare names found in the per-user discs/ become the machine's own copies.
    user_discs = Path(os.environ["BEEBIUM_USER_DIRPATH"]) / "discs"
    user_discs.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(disc, user_discs / "rename-test.ssd")
    shutil.copyfile(hard_disc, user_discs / "rename-test.dat")
    shutil.copyfile(hard_disc.with_suffix(".dsc"), user_discs / "rename-test.dsc")

    created = run("create-machine", "--preset", "model-b-disc", "--floppy", "0:rename-test.ssd",
                  "--acorn-scsi", "--scsi-hdd", "scsi-id=0:image=rename-test.dat",
                  "--output", old)
    assert created.returncode == 0, created.stderr
    uuid = describe(old)["uuid"]
    inode = old.stat().st_ino
    old_name, new_name = old.name.removesuffix(SUFFIX), new.name.removesuffix(SUFFIX)

    def basic(bbc: Beebium, line: str) -> None:
        bbc.keyboard.type(line + "\r")
        assert _run_until(bbc, lambda: _at_prompt(bbc) and bbc.keyboard.typing_status().idle,
                          COMMAND_SECONDS), _screen(bbc)

    with _launch(installation, old) as bbc:
        assert _run_until(bbc, lambda: _at_prompt(bbc), CLIENT_BOOT_SECONDS), _screen(bbc)
        assert bbc.system.identity.name == old_name
        events = bbc.system.watch_status()
        assert next(events).status == ServerStatus.READY

        moved = move(bbc)
        assert moved.name == new_name
        assert moved.uuid == uuid
        changed = next(e for e in events if e.status == ServerStatus.IDENTITY_CHANGED)
        events.close()
        assert changed.identity.name == new_name

        # The directory moved, with its lock and its document file.
        assert not old.exists()
        assert bbc.system.machine_dirpath == str(new)
        assert (new.stat().st_ino != inode) == by_copy
        assert (new / new.name).is_file()
        assert not (new / old.name).exists()
        assert new_name in (new / "desktop.ini").read_bytes().decode("utf-16")
        assert describe(new)["locked_by_pid"]
        assert describe(new)["uuid"] == uuid

        # Its media follow it, and keep taking writes.
        assert quote(new.name) in bbc.disc.drive(0).status.disc_url
        devices = [d for e in bbc.extensions.loaded for d in e.storage_devices]
        assert [Path(d.backing_path).parent for d in devices] == [new]
        basic(bbc, '10 PRINT "MOVED"')
        basic(bbc, 'SAVE "AFTER"')

    with _launch(installation, new) as bbc:
        assert bbc.system.identity.name == new_name
        assert bbc.system.identity.uuid == uuid
        assert _run_until(bbc, lambda: _at_prompt(bbc), CLIENT_BOOT_SECONDS), _screen(bbc)
        basic(bbc, "*CAT")
        assert "AFTER" in _screen(bbc)


def test_a_running_saved_machine_is_renamed_with_its_document(installation, run, describe,
                                                             tmp_path: Path):
    """Renaming a saved machine renames its document: a move beside it."""
    new = tmp_path / "After It.beebium-machine"
    _move_a_running_machine(installation, run, describe, tmp_path / "Before.beebium-machine",
                            new, lambda bbc: bbc.system.set_machine_name("After It"))


def test_a_running_saved_machine_is_moved_to_another_folder(installation, run, describe,
                                                           tmp_path: Path):
    (tmp_path / "Elsewhere").mkdir()
    new = tmp_path / "Elsewhere" / "Moved On.beebium-machine"
    _move_a_running_machine(installation, run, describe, tmp_path / "Before.beebium-machine",
                            new, lambda bbc: bbc.system.move_machine(new).identity)


def test_a_running_saved_machine_is_moved_by_copy_where_it_cannot_be_renamed(
        installation, run, describe, tmp_path: Path, monkeypatch):
    """Across volumes, or on Windows, the machine is copied under its uuid and
    the old directory removed; the server's test seam takes that path here."""
    monkeypatch.setenv("BEEBIUM_MOVE_MACHINE_BY_COPY", "1")
    (tmp_path / "Elsewhere").mkdir()
    new = tmp_path / "Elsewhere" / "Copied On.beebium-machine"
    _move_a_running_machine(installation, run, describe, tmp_path / "Before.beebium-machine",
                            new, lambda bbc: bbc.system.move_machine(new).identity, by_copy=True)


def test_a_saved_machine_refuses_a_name_that_cannot_name_its_document(
        installation, run, tmp_path: Path):
    machine = tmp_path / "Named.beebium-machine"
    taken = tmp_path / "Taken.beebium-machine"
    for directory in (machine, taken):
        assert run("create-machine", "--output", directory).returncode == 0
    with _launch(installation, machine) as bbc:
        for name, code, why in (("a/b", grpc.StatusCode.INVALID_ARGUMENT, "path separator"),
                                ("..", grpc.StatusCode.INVALID_ARGUMENT, "not a file name"),
                                ("Taken", grpc.StatusCode.ALREADY_EXISTS, "already exists")):
            with pytest.raises(grpc.RpcError) as refused:
                bbc.system.set_machine_name(name)
            assert refused.value.code() == code
            assert why in refused.value.details()
        assert bbc.system.set_machine_name("Named").name == "Named"  # the same name: nothing to do
        assert bbc.system.machine_dirpath == str(machine)


def test_a_saved_machine_refuses_a_move_it_cannot_make(installation, run, tmp_path: Path):
    machine = tmp_path / "Named.beebium-machine"
    taken = tmp_path / "Taken.beebium-machine"
    for directory in (machine, taken):
        assert run("create-machine", "--output", directory).returncode == 0
    with _launch(installation, machine) as bbc:
        for destination, code, why in (
                (tmp_path / "No Suffix", grpc.StatusCode.INVALID_ARGUMENT, "does not end with"),
                (tmp_path / "Bad?.beebium-machine", grpc.StatusCode.INVALID_ARGUMENT, "'?'"),
                (machine / "Inner.beebium-machine", grpc.StatusCode.INVALID_ARGUMENT, "inside"),
                (tmp_path / "Missing" / "X.beebium-machine", grpc.StatusCode.NOT_FOUND,
                 "does not exist"),
                (taken, grpc.StatusCode.ALREADY_EXISTS, "already exists")):
            with pytest.raises(grpc.RpcError) as refused:
                bbc.system.move_machine(destination)
            assert refused.value.code() == code, destination
            assert why in refused.value.details()
        assert not (tmp_path / "Missing").exists()  # no parent is made
        assert bbc.system.move_machine(machine).machine_dirpath == str(machine)  # nothing to do
        assert bbc.system.machine_dirpath == str(machine)
        assert (machine / machine.name).is_file()


def test_a_machine_not_yet_saved_is_saved_before_it_is_moved(installation, tmp_path: Path):
    with Beebium.launch(server=installation) as bbc:
        with pytest.raises(grpc.RpcError) as refused:
            bbc.system.move_machine(tmp_path / "Unsaved.beebium-machine")
        assert refused.value.code() == grpc.StatusCode.FAILED_PRECONDITION
        assert "save it first" in refused.value.details()


# ---------------------------------------------------------------------------
# An extension's battery-backed state
# ---------------------------------------------------------------------------


def test_a_saved_machine_keeps_its_clock_until_its_battery_is_reset(installation, run,
                                                                     tmp_path: Path):
    """The acorn-rtc clock runs on its battery: set on one run, it reads the
    same on the next, and a reset of the battery returns it to where its
    configuration starts it."""
    machine = tmp_path / "Clock.beebium-machine"
    created = run("create-machine", "--acorn-rtc",
                  "layout=7bit-year-in-r7:time=1990-01-01T1200", "--output", machine)
    assert created.returncode == 0, created.stderr

    # Its instance id is pinned, so it finds its state on every run.
    preset = json.loads((machine / "preset.json").read_text())
    (rtc_entry,) = [e for e in preset["extensions"] if e["name"] == "acorn-rtc"]
    instance_id = rtc_entry["config"]["id"]
    assert instance_id

    def clock_time() -> tuple[int, int, int, int]:
        with _launch(installation, machine) as bbc:
            now = bbc.extensions[AcornRtc].get_time()
            return now.year, now.month, now.day, now.hour

    assert clock_time() == (1990, 1, 1, 12)
    with _launch(installation, machine) as bbc:
        bbc.extensions[AcornRtc].set_time("2001-02-03T04:05")
    assert clock_time() == (2001, 2, 3, 4)
    assert (machine / "extensions" / instance_id / "nvram" / "saf3019p.bin").is_file()

    assert run("reset-battery", machine).returncode == 0
    assert clock_time() == (1990, 1, 1, 12)


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


def test_a_running_file_server_saved_keeps_what_it_wrote_before_and_after(
        installation, tmp_path: Path, mos_filepath: Path, basic_filepath: Path | None):
    """A file server started from its preset, not yet saved: its writes to its
    disc are its template's copy, lost unless it is saved; saved while it runs,
    it carries on writing to the saved disc."""
    if not NFS_ROM_FILEPATH.is_file():
        pytest.skip(f"{NFS_ROM_FILEPATH.name} not available")
    machine = tmp_path / "Live.beebium-machine"
    ports = {FILE_SERVER_STATION: _free_udp_port(), CLIENT_STATION: _free_udp_port()}
    with Beebium.launch(
        server=installation,
        extra_args=["--preset", "model-b-l3fs-aun", "--aun",
                    _aun_args(ports[FILE_SERVER_STATION], CLIENT_STATION, ports[CLIENT_STATION])],
        startup_timeout=30.0,
    ) as file_server:
        assert _run_until(file_server, lambda: "Ready" in _screen(file_server),
                          FILE_SERVER_BOOT_SECONDS, chunk_seconds=1.0), _screen(file_server)
        assert not _template_media_written(file_server)
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
            assert "No reply" not in _command(client, f"*I AM 0.{FILE_SERVER_STATION} SYST")
            _command(client, "*SAVE BEFORE 3000 +100")
            assert _template_media_written(file_server)

            file_server.system.save_machine(machine)
            assert not _template_media_written(file_server)

            _command(client, "*SAVE AFTER 3000 +100")
            catalogue = _command(client, "*.")
            assert "BEFORE" in catalogue and "AFTER" in catalogue, catalogue
            _command(client, "*BYE")

    def find_both(client: Beebium) -> None:
        catalogue = _command(client, "*.")
        assert "BEFORE" in catalogue and "AFTER" in catalogue, catalogue

    _with_file_server(installation, machine, mos_filepath, basic_filepath, find_both)
