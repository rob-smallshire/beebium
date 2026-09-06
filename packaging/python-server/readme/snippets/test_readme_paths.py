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

"""README snippet: the beebium.server paths API (runs against the installed wheel)."""


def test_paths_api():
    # readme:begin
    import beebium.server

    # Where the bundled server binary lives (ensured executable), and its kin.
    executable = beebium.server.executable_filepath()        # the model-b binary
    print(executable)
    print(beebium.server.variants())                         # the four variants
    print(beebium.server.rom_dirpath())                      # the bundled ROMs
    print(beebium.server.preset_dirpath())                   # the bundled presets
    print(beebium.server.__version__)
    # readme:end
    assert executable.exists()
    assert beebium.server.variants()[0] == "model-b"
    assert beebium.server.rom_dirpath().is_dir()
