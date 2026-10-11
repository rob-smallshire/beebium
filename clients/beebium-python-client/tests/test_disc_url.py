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

"""The file URL the client sends when a disc is inserted by path."""

import sys
from pathlib import Path

import pytest

from beebium.client.disc import _path_to_url


def test_a_path_becomes_its_percent_encoded_file_url():
    url = _path_to_url("/tmp/my game.ssd")
    assert url == Path("/tmp/my game.ssd").resolve().as_uri()
    assert url.startswith("file:///")
    assert "my%20game.ssd" in url


def test_a_path_object_becomes_its_file_url():
    assert _path_to_url(Path("/tmp/game.ssd")) == Path("/tmp/game.ssd").resolve().as_uri()


def test_a_url_is_sent_as_given():
    assert _path_to_url("file:///absolute/game.ssd") == "file:///absolute/game.ssd"
    assert _path_to_url("http://example.com/game.ssd") == "http://example.com/game.ssd"


def test_a_drive_letter_is_not_a_url_scheme():
    # urlparse reads "C:\\x.ssd" as scheme "c"; it is a path, so it must be
    # turned into a file URL rather than sent as given.
    assert _path_to_url("C:\\discs\\game.ssd").startswith("file:///")


@pytest.mark.skipif(sys.platform != "win32", reason="Windows drive paths")
def test_a_windows_drive_path_becomes_file_slash_slash_slash_drive():
    assert _path_to_url("C:\\discs\\game.ssd") == "file:///C:/discs/game.ssd"
