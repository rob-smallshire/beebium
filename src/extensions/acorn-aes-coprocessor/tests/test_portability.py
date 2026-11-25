# Copyright 2026 James Sargent <acheton1984@letterboxes.org>
#
# This file is part of AESulator, a plugin for Beebium.
#
# AESulator is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. AESulator is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with AESulator.
# If not, see <https://www.gnu.org/licenses/>.

# AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
# test_portability.py, tests the path settings and the ship folder
# version 2.0.0, 20 November 2026.

"""The paths file, skipping and the strict setting, the ROM facts as Python reads them,
and the ship folder. None needs a server. Run with run-server-tests.sh. Says nothing of an AES."""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

import aes_host

NEEDS_SERVER = False
SOURCE = Path(os.environ["AES_SOURCE"])
SCRATCH = Path(os.environ["AES_SCRATCH"])
PATHS = SOURCE / "tools/paths.sh"


def clean_env(**more):
    keep = {key: os.environ[key] for key in ("PATH", "HOME")}
    return {**keep, **more}


def paths(folder, line, **env):
    """Sources paths.sh from a folder, with no aes-paths.sh unless the test wrote one."""
    return subprocess.run(["sh", "-c", f'. "{PATHS}"; {line}'], capture_output=True, text=True,
                          env=clean_env(AES_SOURCE=str(folder), **env))


def test_paths_say_what_to_do_when_blank(tmp_path):
    done = paths(tmp_path, "echo reached")
    assert done.returncode == 1 and "reached" not in done.stdout
    assert "aes-paths.sh" in done.stderr


def test_paths_follow_beebium_from_one_folder(tmp_path):
    """DOC, SRC: Beebium's build folder is build/, and its names are BEEBIUM_SERVER, BEEBIUM_ROM_DIR."""
    done = paths(tmp_path, 'echo "$BEEBIUM_BUILD:$BEEBIUM_SERVER:$BEEBIUM_ROM_DIR:$AES_SCRATCH"', BEEBIUM_DIR="/b")
    assert done.stdout.strip() == "/b/build:/b/build/src/server/beebium-model-b:/b/roms:/b/build/aes"


def test_paths_file_is_read_and_a_set_variable_wins(tmp_path):
    (tmp_path / "aes-paths.sh").write_text(': "${BEEBIUM_DIR:=/from-file}"\n: "${BEEBIUM_BUILD:=/built}"\n')
    assert paths(tmp_path, 'echo "$BEEBIUM_DIR $BEEBIUM_BUILD"').stdout.strip() == "/from-file /built"
    done = paths(tmp_path, 'echo "$BEEBIUM_DIR $BEEBIUM_BUILD"', BEEBIUM_DIR="/from-env")
    assert done.stdout.strip() == "/from-env /built"


def test_moved_beebium_stops_with_a_plain_message(tmp_path):
    done = paths(tmp_path, "need_beebium; echo reached", BEEBIUM_DIR=str(tmp_path / "gone"))
    assert done.returncode == 1 and "reached" not in done.stdout
    assert "No Beebium at BEEBIUM_DIR" in done.stderr


def test_build_makes_no_copy_of_beebium_unasked(tmp_path):
    """The old script quietly made a fresh copy at the old place. A copy of the two scripts runs, so the
    real aes-paths.sh is never read and no real build can start."""
    (tmp_path / "tools").mkdir()
    for name in ("beebium-build.sh", "paths.sh"):
        shutil.copy(SOURCE / "tools" / name, tmp_path / "tools")
    gone = tmp_path / "gone"
    done = subprocess.run(["sh", str(tmp_path / "tools/beebium-build.sh")], capture_output=True, text=True,
                          env=clean_env(BEEBIUM_DIR=str(gone), AES_SCRATCH=str(tmp_path)))
    assert done.returncode == 1 and not gone.exists()
    assert "No Beebium at BEEBIUM_DIR" in done.stderr


def headless(**env):
    binary = SCRATCH / "plugin-tests/tests"
    if not binary.exists():
        aes_host.missing("the headless tests are not built: run run-tests.sh first")
    return subprocess.run([binary], capture_output=True, text=True, errors="replace",
                          env=clean_env(AES_PLUGIN_DIR=str(SCRATCH / "plugin-tests/src"), **env))


def test_headless_skips_without_a_rom_and_counts():
    done = headless()
    assert done.returncode == 0, done.stdout[-500:]
    assert "SKIPPED: " in done.stdout and "no ROM: AES_ROM is not set" in done.stdout
    assert re.search(r"tests, 0 failed, [1-9][0-9]* skipped;", done.stdout)


def test_headless_strict_turns_a_skip_into_a_failure():
    done = headless(AES_REQUIRE_ALL="1")
    assert done.returncode == 1
    assert "skipped under AES_REQUIRE_ALL" in done.stdout


def host(line, **env):
    """aes_host in a fresh process, with only the variables given."""
    return subprocess.run([sys.executable, "-c", f"import aes_host; {line}"], capture_output=True, text=True,
                          cwd=Path(aes_host.__file__).parent, env=clean_env(**env))


def test_host_module_imports_with_nothing_set():
    done = host("print(aes_host.rom_problem())")
    assert done.returncode == 0, done.stderr
    assert "no ROM: AES_ROM is not set" in done.stdout


def test_unknown_rom_gives_a_plain_reason(tmp_path):
    other = tmp_path / "other.rom"
    other.write_bytes(bytes(0x4000))
    done = host("print(aes_host.rom_problem())", AES_ROM=str(other))
    assert f"no facts file for this ROM, SHA-1 {aes_host.sha1(other)}" in done.stdout


def test_facts_keep_trailing_spaces(tmp_path):
    facts = tmp_path / "facts.txt"
    facts.write_text("# a comment\n\nplain=1\nspaced=two words \nsum=a=b\n")
    assert aes_host.read_facts(facts) == {"plain": "1", "spaced": "two words ", "sum": "a=b"}


def need_ship_tools():
    if not (SOURCE / "docs/ship").is_dir() or not (SOURCE / "tools/make-ship.sh").is_file():
        aes_host.missing("the ship tests need the project's docs/ship folder and tools/make-ship.sh, "
                         "which are not shipped")


def ship(tmp_path, *more):
    need_ship_tools()
    folder = tmp_path / "ship folder"
    done = subprocess.run(["sh", str(SOURCE / "tools/make-ship.sh"), str(folder), *more], capture_output=True,
                          text=True, env=clean_env(AES_PRIVATE_WORDS=os.environ.get("AES_PRIVATE_WORDS", "")))
    assert done.returncode == 0, done.stdout + done.stderr
    return folder


def check(folder, *more, **env):
    return subprocess.run([sys.executable, str(SOURCE / "tools/check-ship.py"), str(folder), str(SOURCE)],
                          capture_output=True, text=True, env=clean_env(**env))


def test_ship_folder_is_clean_and_matches(tmp_path):
    folder = ship(tmp_path)
    assert (folder / "acorn-aes-coprocessor/ArmCore.cpp").is_file() and (folder / "aes-paths.template.sh").is_file()
    assert (folder / "docs/user-guide.md").is_file() and sorted(p.name for p in (folder / "docs").iterdir()) == ["arm1-and-aes.md", "user-guide.md"]
    assert not (folder / "aes-paths.sh").exists() and not (folder / "notes").exists()
    assert not (folder / "tools/git.sh").exists()
    for script in ("tools/plugin-build.sh", "tools/beebium-build.sh", "acorn-aes-coprocessor/tests/run-tests.sh",
                   "acorn-aes-coprocessor/tests/run-server-tests.sh"):
        assert os.access(folder / script, os.X_OK), f"{script} has no executable mark"


def test_ship_check_fails_on_an_absolute_path(tmp_path):
    folder = ship(tmp_path)
    path = "/" + "Users/someone/x"
    (folder / "docs/user-guide.md").write_text(path)
    assert check(folder).returncode == 1


def test_ship_check_fails_on_a_private_word(tmp_path):
    folder = ship(tmp_path)
    (folder / "docs/extra.md").write_text("a Word here")
    done = check(folder, AES_PRIVATE_WORDS="elsewhere|word")
    assert done.returncode == 1 and "docs/extra.md" in done.stdout


@pytest.mark.parametrize("sha1", ["15e15c78db34cfa22a83689be0de30704b7a4c67", "74af1e9f28518cd792e6edb53f6f3a30d4b0de48",
                                  "ba33eaf1a23cfef6fc1b88aa516ca2b3693e69d9", "abcc71cbc43489e89a87aac64e67b17daef5895a",
                                  "bfa252de2af0d20db87af554fff99f158e267ace"])
def test_ship_check_fails_on_a_facts_file_not_marked_to_ship_but_not_on_its_hash(tmp_path, sha1):
    """Owner, 18 Nov 2026: a facts file ships only with ships=yes, its ROM passing every test."""
    folder = ship(tmp_path)
    (folder / "docs/user-guide.md").write_text(f"{sha1[:8]}, named in the text\n")
    assert check(folder).returncode == 0
    held = f"acorn-aes-coprocessor/tests/rom-facts/{sha1}.txt"
    (folder / held).write_text("# not marked\n")
    done = check(folder)
    assert done.returncode == 1 and f"{held}: a ROM facts file not marked to ship (no ships=yes)" in done.stdout


def test_ship_check_fails_when_a_version_is_not_the_one_held(tmp_path):
    folder = ship(tmp_path)
    paths = folder / "tools/paths.sh"
    paths.write_text(re.sub(r"(?m)^# version [^,]+,", "# version 1.0.1,", paths.read_text(), count=1))
    done = check(folder)
    assert done.returncode == 1 and "a version line says 'version 1.0.1," in done.stdout


def test_ship_check_fails_on_a_rom_or_a_disc(tmp_path):
    for name in ("x.rom", "x.ssd"):
        folder = ship(tmp_path)
        (folder / "acorn-aes-coprocessor" / name).write_bytes(b"0")
        assert check(folder).returncode == 1


def test_ship_check_fails_when_it_differs_from_plugin(tmp_path):
    folder = ship(tmp_path)
    (folder / "acorn-aes-coprocessor/ArmCore.cpp").write_text("changed by hand\n")
    assert check(folder).returncode == 1


def test_ship_check_fails_on_a_script_without_its_mark(tmp_path):
    folder = ship(tmp_path)
    (folder / "tools/plugin-build.sh").chmod(0o644)
    done = check(folder)
    assert done.returncode == 1 and "tools/plugin-build.sh" in done.stdout


def test_ship_check_fails_on_a_macos_leftover(tmp_path):
    folder = ship(tmp_path)
    (folder / "docs/.DS_Store").write_bytes(b"0")
    assert check(folder).returncode == 1


def test_ship_refuses_a_folder_it_did_not_make(tmp_path):
    need_ship_tools()
    folder = tmp_path / "somewhere"
    folder.mkdir()
    (folder / "keep").write_text("mine")
    done = subprocess.run(["sh", str(SOURCE / "tools/make-ship.sh"), str(folder)], capture_output=True, text=True,
                          env=clean_env())
    assert done.returncode == 1 and (folder / "keep").exists()
