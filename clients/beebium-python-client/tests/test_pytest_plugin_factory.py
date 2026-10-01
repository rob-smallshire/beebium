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

"""The pytest plugin's launch_bbc factory and the bbc configuration fixtures (#105).

Each test runs an inner pytest session with pytester, so a nested conftest can
override the plugin's fixtures exactly as a downstream project's would. The
inner session is pointed at the same server and ROMs as this one, since its
rootdir is a temporary directory outside the checkout.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

from beebium.client.installation import ServerInstallation
from beebium.client.pytest_plugin import PRESET_EXTENSION, resolve_preset

pytest_plugins = ["pytester"]


def _pid_is_alive(pid: int) -> bool:
    """Whether a process with this PID is running.

    os.kill(pid, 0) is the probe on POSIX, but on Windows it is not: there it
    raises OSError (WinError 87), so Windows asks the process for its exit code.
    """
    if sys.platform == "win32":
        import ctypes

        process_query_limited_information = 0x1000
        error_access_denied = 5
        still_active = 259
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(process_query_limited_information, False, pid)
        if not handle:
            return kernel32.GetLastError() == error_access_denied
        try:
            exit_code = ctypes.c_ulong()
            if not kernel32.GetExitCodeProcess(handle, ctypes.byref(exit_code)):
                return False
            return exit_code.value == still_active
        finally:
            kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


REPO_ROOT = Path(__file__).resolve().parents[3]
DISC_FILEPATH = REPO_ROOT / "tests" / "assets" / "discs" / "Disc001-CylonAttackAFSTD.ssd"


@pytest.fixture
def inner_args(
    beebium_roms_dirpath: Path,
    beebium_server_filepath: Path | None,
    server_installation: ServerInstallation,
) -> list[str]:
    """Command-line options that point an inner session at this session's
    server and ROMs. Requesting this skips the test where there is no server."""
    args = ["-p", "no:cacheprovider", f"--beebium-rom-dir={beebium_roms_dirpath}"]
    if beebium_server_filepath is not None:
        args.append(f"--beebium-server={beebium_server_filepath}")
    return args


@pytest.fixture
def disc_filepath() -> Path:
    if not DISC_FILEPATH.exists():
        pytest.skip(f"test disc image not found: {DISC_FILEPATH}")
    return DISC_FILEPATH


def _boot_disc_test(disc_filepath: Path, *, expect_controller: bool) -> str:
    """An inner test that boots a disc and asserts whether that worked."""
    if expect_controller:
        body = "    bbc.boot_disc(DISC)\n"
    else:
        body = "    with pytest.raises(Exception, match='no disc controller'):\n        bbc.boot_disc(DISC)\n"
    return f"import pytest\nDISC = {str(disc_filepath)!r}\ndef test_boot(bbc):\n    bbc.expect('BASIC')\n{body}"


def test_the_default_bbc_is_a_bare_model_b(
    pytester: pytest.Pytester, inner_args: list[str], disc_filepath: Path
) -> None:
    pytester.makepyfile(_boot_disc_test(disc_filepath, expect_controller=False))
    result = pytester.runpytest(*inner_args)
    result.assert_outcomes(passed=1)


def test_overriding_beebium_preset_in_a_conftest_configures_bbc(
    pytester: pytest.Pytester, inner_args: list[str], disc_filepath: Path
) -> None:
    pytester.makeconftest(
        "import pytest\n@pytest.fixture(scope='session')\ndef beebium_preset():\n    return 'model-b-disc'\n"
    )
    pytester.makepyfile(_boot_disc_test(disc_filepath, expect_controller=True))
    result = pytester.runpytest(*inner_args)
    result.assert_outcomes(passed=1)


def test_a_preset_file_path_configures_bbc(
    pytester: pytest.Pytester,
    inner_args: list[str],
    disc_filepath: Path,
    server_installation: ServerInstallation,
) -> None:
    preset_filepath = resolve_preset("model-b-disc", server=server_installation)
    pytester.makeconftest(
        f"import pytest\n@pytest.fixture(scope='session')\ndef beebium_preset():\n    return {str(preset_filepath)!r}\n"
    )
    pytester.makepyfile(_boot_disc_test(disc_filepath, expect_controller=True))
    result = pytester.runpytest(*inner_args)
    result.assert_outcomes(passed=1)


def test_the_beebium_preset_option_configures_bbc(
    pytester: pytest.Pytester, inner_args: list[str], disc_filepath: Path
) -> None:
    pytester.makepyfile(_boot_disc_test(disc_filepath, expect_controller=True))
    result = pytester.runpytest(*inner_args, "--beebium-preset=model-b-disc")
    result.assert_outcomes(passed=1)


def test_the_beebium_preset_ini_key_configures_bbc(
    pytester: pytest.Pytester, inner_args: list[str], disc_filepath: Path
) -> None:
    pytester.makeini("[pytest]\nbeebium_preset = model-b-disc\n")
    pytester.makepyfile(_boot_disc_test(disc_filepath, expect_controller=True))
    result = pytester.runpytest(*inner_args)
    result.assert_outcomes(passed=1)


def test_a_conftest_override_outranks_the_option(
    pytester: pytest.Pytester, inner_args: list[str], disc_filepath: Path
) -> None:
    pytester.makeconftest("import pytest\n@pytest.fixture(scope='session')\ndef beebium_preset():\n    return None\n")
    pytester.makepyfile(_boot_disc_test(disc_filepath, expect_controller=False))
    result = pytester.runpytest(*inner_args, "--beebium-preset=model-b-disc")
    result.assert_outcomes(passed=1)


def test_beebium_extra_args_reach_the_server(
    pytester: pytest.Pytester, inner_args: list[str], disc_filepath: Path
) -> None:
    pytester.makeconftest(
        "import pytest\n"
        "@pytest.fixture(scope='session')\n"
        "def beebium_extra_args():\n"
        "    return ['--fdc', 'acorn-1770']\n"
    )
    # A controller with no filing system ROM: the disc mounts, which it cannot
    # on a machine with no controller at all.
    pytester.makepyfile(
        "import pytest\n"
        f"DISC = {str(disc_filepath)!r}\n"
        "def test_mount(bbc):\n"
        "    bbc.expect('BASIC')\n"
        "    bbc.disc.drive(0).insert(DISC)\n"
    )
    result = pytester.runpytest(*inner_args)
    result.assert_outcomes(passed=1)


def test_launch_bbc_launches_several_machines_and_tears_them_all_down(
    pytester: pytest.Pytester, inner_args: list[str], tmp_path: Path
) -> None:
    pids_filepath = tmp_path / "pids.txt"
    pytester.makepyfile(
        f"PIDS = {str(pids_filepath)!r}\n"
        "def test_two(launch_bbc):\n"
        "    model_b = launch_bbc()\n"
        "    b_plus = launch_bbc(variant='model-b-plus')\n"
        "    model_b.expect('BASIC')\n"
        "    b_plus.expect('BASIC')\n"
        "    # The server processes, to check from outside once the test is over.\n"
        "    pids = [m._server._process.pid for m in (model_b, b_plus)]\n"
        "    assert len(set(pids)) == 2\n"
        "    open(PIDS, 'w').write(' '.join(map(str, pids)))\n"
    )
    result = pytester.runpytest(*inner_args)
    result.assert_outcomes(passed=1)

    for pid in map(int, pids_filepath.read_text().split()):
        assert not _pid_is_alive(pid), f"server {pid} outlived the test"


def test_bbc_tube_boots_with_the_coprocessor_running(pytester: pytest.Pytester, inner_args: list[str]) -> None:
    pytester.makepyfile("def test_tube(bbc_tube):\n    bbc_tube.expect('BASIC')\n    bbc_tube.expect('TUBE')\n")
    result = pytester.runpytest(*inner_args)
    result.assert_outcomes(passed=1)


def test_launch_bbc_skips_when_no_server_can_be_found(
    pytester: pytest.Pytester,
    beebium_roms_dirpath: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # BEEBIUM_SERVER defers server resolution to launch time, where a path
    # that is not an executable raises ServerNotFoundError.
    monkeypatch.setenv("BEEBIUM_SERVER", str(tmp_path / "no-such-server"))
    pytester.makepyfile("def test_launch(bbc):\n    raise AssertionError('should have been skipped')\n")
    result = pytester.runpytest("-p", "no:cacheprovider", f"--beebium-rom-dir={beebium_roms_dirpath}")
    result.assert_outcomes(skipped=1)


def test_resolve_preset_returns_an_existing_path_unchanged(tmp_path: Path) -> None:
    preset_filepath = tmp_path / f"mine{PRESET_EXTENSION}"
    preset_filepath.write_text("{}")
    assert resolve_preset(preset_filepath) == preset_filepath


def test_resolve_preset_finds_an_id_in_the_searched_directories(tmp_path: Path) -> None:
    first = tmp_path / "system"
    second = tmp_path / "user"
    first.mkdir()
    second.mkdir()
    preset_filepath = second / f"mine{PRESET_EXTENSION}"
    preset_filepath.write_text("{}")
    assert resolve_preset("mine", search_dirpaths=[first, second]) == preset_filepath


def test_resolve_preset_names_where_it_looked_for_an_unknown_id(tmp_path: Path) -> None:
    with pytest.raises(FileNotFoundError, match=r"no-such-preset.*searched: .*system"):
        resolve_preset("no-such-preset", search_dirpaths=[tmp_path / "system"])


def test_resolve_preset_says_when_there_is_no_server_to_search(tmp_path: Path) -> None:
    with pytest.raises(FileNotFoundError, match="no server was found"):
        resolve_preset("model-b-disc", server=tmp_path / "no-such-server")


def test_resolve_preset_finds_a_system_preset_by_id(server_installation: ServerInstallation) -> None:
    resolved = resolve_preset("model-b-disc", server=server_installation)
    assert resolved.name == f"model-b-disc{PRESET_EXTENSION}"
    assert resolved.is_file()
