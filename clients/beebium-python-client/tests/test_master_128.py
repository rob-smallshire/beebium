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

"""The BBC Master 128 server (#189): the master-128 preset boots MOS 3.20 to
BASIC in Mode 7 on ADFS, and a saved machine keeps its *CONFIGURE settings in
the clock's battery-backed CMOS from one run to the next.

The paging rules, the clock on the System VIA and the CMOS seed are tested in
C++ (tests/test_master_128_*.cpp).
"""

from __future__ import annotations

import json
import re
import subprocess
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import BeebiumError, ServerNotFoundError
from beebium.client.installation import ServerInstallation
from beebium.client.sideways import SlotType

VARIANT = "master-128"
PRESET = "master-128"
# OSBYTE &87: the screen mode in Y.
PRINT_MODE = 'A%=&87:PRINT "MODE=";(USR(&FFF4) AND &FF0000) DIV &10000'


def _screen(bbc: Beebium) -> str:
    return bbc.video.screen_text().text


def _at_prompt(bbc: Beebium, containing: str) -> bool:
    lines = [ln.rstrip() for ln in _screen(bbc).splitlines() if ln.strip()]
    return (bbc.keyboard.typing_status().idle and bool(lines) and lines[-1] == ">"
            and containing in _screen(bbc))


def _booted(bbc: Beebium) -> None:
    bbc.system.set_speed_multiplier(0.0)
    assert bbc.run_until_or_timeout(lambda: _at_prompt(bbc, "BASIC"), 10.0), _screen(bbc)


def _type(bbc: Beebium, line: str, then_containing: str) -> None:
    bbc.keyboard.type(line + "\r")
    assert bbc.run_until_or_timeout(lambda: _at_prompt(bbc, then_containing), 10.0), \
        _screen(bbc)


@pytest.fixture
def installation(beebium_server_filepath: Path | None) -> ServerInstallation:
    try:
        installation = (ServerInstallation.default() if beebium_server_filepath is None
                        else ServerInstallation.coerce(beebium_server_filepath))
        installation.command(VARIANT)
        return installation
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.fixture
def master_command(installation: ServerInstallation) -> list[str]:
    """The command that runs the Master, for its arguments to follow."""
    return installation.command(VARIANT)


def test_boots_mos_3_20_to_basic_in_mode_7_on_adfs(launch_bbc) -> None:
    bbc = launch_bbc(preset=PRESET, variant=VARIANT)
    _booted(bbc)
    screen = _screen(bbc)
    assert "Acorn MOS" in screen
    assert "Acorn ADFS" in screen
    _type(bbc, PRINT_MODE, "MODE=")
    assert re.search(r"MODE=\s*7", _screen(bbc)), _screen(bbc)


def test_configure_survives_save_and_relaunch(installation: ServerInstallation,
                                              master_command: list[str],
                                              tmp_path: Path) -> None:
    machine = tmp_path / "Master.beebium-machine"
    created = subprocess.run(
        [*master_command, "create-machine", "--preset", PRESET, "--output",
         str(machine)],
        capture_output=True, text=True, timeout=60)
    assert created.returncode == 0, created.stderr

    with Beebium.launch(server=installation, variant=VARIANT,
                        extra_args=["--machine", str(machine)]) as bbc:
        _booted(bbc)
        _type(bbc, "*CONFIGURE MODE 0", ">")
    assert (machine / "nvram" / "master-128-cmos.bin").is_file()

    with Beebium.launch(server=installation, variant=VARIANT,
                        extra_args=["--machine", str(machine)]) as bbc:
        _booted(bbc)
        _type(bbc, PRINT_MODE, "MODE=")
        assert re.search(r"MODE=\s*0", _screen(bbc)), _screen(bbc)


def test_keyboard_links_do_not_apply(master_command: list[str]) -> None:
    """The Master's keyboard has no start-up links: its boot options are in
    CMOS, so the server refuses the options that set links."""
    result = subprocess.run([*master_command, "start", "--auto-boot"],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode != 0
    assert "no start-up links" in result.stderr


def test_preset_schema_has_no_startup_links_and_names_the_mos_roms(
        master_command: list[str]) -> None:
    result = subprocess.run([*master_command, "describe-preset-schema"],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    schema = json.loads(result.stdout)
    assert "startup" not in [section["type"] for section in schema["sections"]]
    default_roms = {rom["slot"]: rom["image"]
                    for rom in _find(schema, "default_roms")}
    assert default_roms[12] == "bbc-basic_4.rom"
    assert default_roms[9] == "acorn-dfs_2_24.rom"
    assert default_roms[15] == "acorn-terminal_1_20_mos_3_20.rom"


def _find(node, key: str):
    """The first value under `key` anywhere in a JSON document."""
    if isinstance(node, dict):
        if key in node:
            return node[key]
        children = node.values()
    elif isinstance(node, list):
        children = node
    else:
        return None
    for child in children:
        found = _find(child, key)
        if found is not None:
            return found
    return None


# ---------------------------------------------------------------------------
# The 1770 (step 3 of docs/plans/master-128-round-1.md)
# ---------------------------------------------------------------------------


def _command(bbc: Beebium, line: str, then_containing: str, seconds: float = 20.0) -> str:
    bbc.keyboard.type("CLS\r")
    assert bbc.run_until_or_timeout(lambda: _at_prompt(bbc, ">"), 10.0), _screen(bbc)
    bbc.keyboard.type(line + "\r")
    assert bbc.run_until_or_timeout(
        lambda: _at_prompt(bbc, then_containing) and _screen(bbc).strip() != ">", seconds), \
        _screen(bbc)
    return _screen(bbc)


def test_adfs_catalogues_a_floppy(launch_bbc, tmp_path: Path) -> None:
    bbc = launch_bbc(preset=PRESET, variant=VARIANT)
    _booted(bbc)
    disc = bbc.disc.create_disc("Acorn ADFS S 160K", tmp_path / "blank.ads")
    bbc.disc.drive(0).insert(disc)
    _command(bbc, "*MOUNT 0", ">")
    screen = _command(bbc, "*CAT", "Drive:0")
    assert re.search(r"Dir\.\s*\$", screen), screen


def test_dfs_catalogues_saves_and_loads(launch_bbc, tmp_path: Path) -> None:
    bbc = launch_bbc(preset=PRESET, variant=VARIANT)
    _booted(bbc)
    disc = bbc.disc.create_disc("Acorn DFS 80T SS 200K", tmp_path / "blank.ssd")
    bbc.disc.drive(0).insert(disc)
    _command(bbc, "*DISC", ">")
    _command(bbc, '10 PRINT "MASTER DISC"', ">")
    _command(bbc, 'SAVE "PROG"', ">")
    screen = _command(bbc, "*CAT", "PROG")
    assert re.search(r"Drive\s*:?\s*0", screen), screen
    _command(bbc, "NEW", ">")
    _command(bbc, 'LOAD "PROG"', ">")
    screen = _command(bbc, "LIST", "MASTER DISC")
    assert '10 PRINT "MASTER DISC"' in screen, screen


def test_the_fdc_name_placeholder_names_the_1770(launch_bbc) -> None:
    bbc = launch_bbc(preset=PRESET, variant=VARIANT)
    preview = bbc.system.preview_machine_description("Master {fdc-controller}")
    assert preview.description == "Master WD1770"
    assert preview.report.inapplicable_keys == ()


# ---------------------------------------------------------------------------
# The small moves (step 4)
# ---------------------------------------------------------------------------


def test_station_is_cmos_byte_0(launch_bbc) -> None:
    """--station 42 is the Master's CMOS byte 0, which the guest reads with
    OSBYTE &A1, and the station number Econet reports."""
    bbc = launch_bbc(preset=PRESET, variant=VARIANT, extra_args=["--station", "42"])
    _booted(bbc)
    status = bbc.econet.status
    assert status.station_id == 42
    assert status.station_in_force == 42
    _command(bbc, 'A%=&A1:X%=0:PRINT "S=";(USR(&FFF4) AND &FF0000) DIV &10000', "S=")
    assert re.search(r"S=\s*42", _screen(bbc)), _screen(bbc)


# ---------------------------------------------------------------------------
# The two Tubes (step 5)
# ---------------------------------------------------------------------------

BANNER_65C102 = "Acorn TUBE 65C102 Co-Processor"


def test_a_coprocessor_on_the_internal_tube_boots(launch_bbc) -> None:
    """The master-128-65c102-copro preset: a Master Turbo in all but name."""
    bbc = launch_bbc(preset="master-128-65c102-copro", variant=VARIANT, startup_timeout=30.0)
    _booted(bbc)
    assert BANNER_65C102 in _screen(bbc), _screen(bbc)
    assert bbc.tube.status.enabled


def test_mos_finds_an_external_coprocessor_with_intube_configured(launch_bbc) -> None:
    """The cross-probe: the seed says INTUBE, the coprocessor is on the
    external Tube, and MOS 3.20 still finds it by toggling ITU."""
    bbc = launch_bbc(preset=PRESET, variant=VARIANT, extra_args=["--tube-65c102"],
                     startup_timeout=30.0)
    _booted(bbc)
    assert BANNER_65C102 in _screen(bbc), _screen(bbc)
    assert bbc.tube.status.enabled


def test_two_coprocessors_are_refused(master_command: list[str]) -> None:
    result = subprocess.run(
        [*master_command, "start", "--tube-65c102", "--tube-65c02", "at=internal-tube"],
        capture_output=True, text=True, timeout=60)
    assert result.returncode != 0
    assert "serves one coprocessor at a time" in result.stderr


def test_a_model_b_has_no_internal_tube(installation: ServerInstallation) -> None:
    try:
        model_b = installation.command("model-b")
    except ServerNotFoundError:
        pytest.skip("beebium-model-b not built")
    result = subprocess.run([*model_b, "start", "--tube-65c102", "at=internal-tube"],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode != 0
    assert "internal-tube" in result.stderr


# ---------------------------------------------------------------------------
# Cartridges, IFJ, LK18/LK19 (step 6)
# ---------------------------------------------------------------------------

REPO_ROMS_DIRPATH = Path(__file__).parents[3] / "roms"


def test_a_rom_cartridge_shows_its_banks_read_only(launch_bbc) -> None:
    """A cartridge with a ROM in its even-bank socket only, in the rear slot:
    bank 0 answers with it and bank 1 is empty, as on Acorn's cartridge.
    The ROM is bank 0's ordinary sideways entry. SidewaysService shows the
    banks and will not change them."""
    # A bare ROM name is found on the ROM search path, as for any bank.
    rom = REPO_ROMS_DIRPATH / "bbc-basic_2.rom"
    bbc = launch_bbc(preset=PRESET, variant=VARIANT,
                     extra_args=["--acorn-rom-cartridge",
                                 "--sideways", "slot=0:type=rom:image=bbc-basic_2.rom"])
    _booted(bbc)
    status = bbc.sideways.get_slot_status()
    socket = status.find_socket_for_slot(0)
    assert socket is not None
    assert socket.type == SlotType.ROM
    assert socket.populated
    assert Path(socket.image_filepath).name == rom.name
    empty = status.find_socket_for_slot(1)
    assert not empty.populated
    assert empty.image_filepath == ""
    assert not status.find_socket_for_slot(2).populated  # the front slot is empty
    with pytest.raises(BeebiumError, match="rear cartridge slot"):
        bbc.sideways.configure_slot(0, SlotType.EMPTY)
    # The MOS sees the cartridge's ROM once, in bank 0.
    screen = _command(bbc, "*ROMS", "ROM 0")
    assert re.search(r"ROM\s+0\s+BASIC", screen), screen
    assert not re.search(r"ROM\s+1\s+BASIC", screen), screen


def test_a_cartridge_with_its_odd_socket_only_in_the_front_slot(launch_bbc) -> None:
    rom = REPO_ROMS_DIRPATH / "bbc-basic_2.rom"
    bbc = launch_bbc(preset=PRESET, variant=VARIANT,
                     extra_args=["--acorn-rom-cartridge", "at=cartridge-front",
                                 "--sideways", f"slot=3:type=rom:image={rom}"])
    _booted(bbc)
    status = bbc.sideways.get_slot_status()
    assert status.find_socket_for_slot(3).populated
    assert not status.find_socket_for_slot(2).populated
    assert not status.find_socket_for_slot(0).populated


def test_a_cartridge_with_no_roms_has_two_empty_banks(launch_bbc) -> None:
    bbc = launch_bbc(preset=PRESET, variant=VARIANT,
                     extra_args=["--acorn-rom-cartridge", "at=cartridge-rear"])
    _booted(bbc)
    status = bbc.sideways.get_slot_status()
    assert not status.find_socket_for_slot(0).populated
    assert not status.find_socket_for_slot(1).populated


def test_a_cartridge_bank_s_rom_needs_a_cartridge(master_command: list[str], tmp_path: Path) -> None:
    """A preset's entry for a cartridge bank with no cartridge at that slot is
    refused in the preset's terms, naming the slot to fit one at."""
    preset_filepath = tmp_path / "orphan.json"
    preset_filepath.write_text(json.dumps({
        "model": "master-128",
        "sideways_bank": {"slots": [{"slot": 2, "type": "rom", "image_uri": "bbc-basic_2.rom"}]},
    }))
    result = subprocess.run([*master_command, "resolve-preset", str(preset_filepath)],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode != 0
    assert "Fit a cartridge at the Front Cartridge Slot under Peripherals" in result.stderr
    assert "--sideways" not in result.stderr


def test_a_preset_s_cartridge_rom_is_its_bank_s_entry(master_command: list[str], tmp_path: Path) -> None:
    """A fitted cartridge's ROM is an ordinary sideways_bank entry, which
    resolve-preset writes back as given, with no marker."""
    entry = {"slot": 0, "type": "rom", "image_uri": "bbc-basic_2.rom"}
    preset_filepath = tmp_path / "cartridge.json"
    preset_filepath.write_text(json.dumps({
        "model": "master-128",
        "sideways_bank": {"slots": [entry]},
        "extensions": [{"name": "acorn-rom-cartridge", "config": {"at": "cartridge-rear"}}],
    }))
    result = subprocess.run([*master_command, "resolve-preset", str(preset_filepath)],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    resolved = json.loads(result.stdout)
    assert entry in resolved["sideways_bank"]["slots"]
    cartridge = resolved["extensions"][0]
    assert cartridge["name"] == "acorn-rom-cartridge"
    assert "low" not in cartridge["config"] and "high" not in cartridge["config"]


def test_a_cartridge_takes_no_image_parameters(master_command: list[str]) -> None:
    result = subprocess.run([*master_command, "start", "--acorn-rom-cartridge", "low=bbc-basic_2.rom"],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode != 0


def test_the_cartridge_s_old_name_is_not_an_extension(master_command: list[str], tmp_path: Path) -> None:
    """The extension was renamed from rom-cartridge to acorn-rom-cartridge with
    no alias: a preset naming the old one is refused like any unknown name."""
    preset_filepath = tmp_path / "old-cartridge.json"
    preset_filepath.write_text(json.dumps({
        "model": "master-128",
        "extensions": [{"name": "rom-cartridge"}],
    }))
    result = subprocess.run([*master_command, "resolve-preset", str(preset_filepath)],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode != 0
    assert "'rom-cartridge' is not a known extension" in result.stderr


def test_lk18_east_takes_a_rom_in_banks_4_and_5(launch_bbc) -> None:
    rom = REPO_ROMS_DIRPATH / "bbc-basic_2.rom"
    bbc = launch_bbc(preset=PRESET, variant=VARIANT,
                     extra_args=["--motherboard-link", "lk18=east",
                                 "--sideways", f"slot=4:type=rom:image={rom}"])
    _booted(bbc)
    status = bbc.sideways.get_slot_status()
    assert status.find_socket_for_slot(4).type == SlotType.ROM
    assert status.find_socket_for_slot(6).type == SlotType.RAM


# ---------------------------------------------------------------------------
# Presets (step 8)
# ---------------------------------------------------------------------------


def test_the_mos_3_50_preset_boots_to_basic_on_adfs(launch_bbc) -> None:
    bbc = launch_bbc(preset="master-128-mos-3-50", variant=VARIANT)
    _booted(bbc)
    screen = _screen(bbc)
    assert "ACORN MOS" in screen, screen
    assert "Acorn ADFS" in screen, screen


def test_the_master_lists_its_presets(master_command: list[str]) -> None:
    result = subprocess.run([*master_command, "--format", "tsv", "list-presets"],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    ids = {line.split("\t")[0] for line in result.stdout.splitlines()[1:]}
    assert {"master-128", "master-128-mos-3-50", "master-128-65c102-copro", "master-128-aes-copro"} <= ids
