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

"""Proof that a coprocessor boots from its own packaged client ROM.

The Tube client ROMs no longer live in any shared roms/ directory or in
BEEBIUM_ROM_DIR -- each ships inside its plugin's own roms/ directory and the
extension resolves it against its manifest directory, not through the host ROM
search. So booting the coprocessor's banner proves the server loaded the
packaged ROM from the plugin; there is nowhere else it could come from. Each
coprocessor has its own distinct banner.
"""

from __future__ import annotations

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains, dump_screen


@pytest.mark.slow
@pytest.mark.timeout(60)
@pytest.mark.parametrize(
    "tube_flag,banner",
    [
        ("--tube-65c02", "Acorn TUBE 6502 64K"),
        ("--tube-65c102", "Acorn TUBE 65C102 Co-Processor"),
    ],
)
def test_packaged_tube_rom_boots_banner(
    server_filepath, mos_filepath, basic_filepath, dfs_filepath,
    tube_flag, banner,
):
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                tube_flag,
                "--fdc", "acorn-1770",
                "--sideways", f"slot=14:type=rom:image={dfs_filepath}",
            ],
            startup_timeout=30.0,
        ) as bbc:
            ok = bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, banner),
                emulated_seconds=30.0,
            )
            assert ok, (
                f"{tube_flag} did not show its packaged-ROM banner "
                f"{banner!r}:\n{dump_screen(bbc)}"
            )
    except ServerNotFoundError as e:
        pytest.skip(str(e))
