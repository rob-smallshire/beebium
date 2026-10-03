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

"""Machine names as templates, against a real server (issue #153).

A machine's name is a template whose placeholders the server fills from the
machine's state: launched as ``Station {econet-station}`` with station 80, it
is called "Station 80". The client knows no placeholder keys of its own; it
reads the template and the rendering, lists what the server offers, previews,
and sets. See docs/discussion/machine-name-templates.md.
"""

from __future__ import annotations

import time
from collections.abc import Iterator
from pathlib import Path

import grpc
import pytest

from beebium.client import Beebium
from beebium.client.installation import ServerInstallation

TEMPLATE = "Station {econet-station}"

# The server re-renders about once a second; allow for a loaded runner.
RENDER_TIMEOUT = 10.0


@pytest.fixture
def station_80(
    mos_filepath: Path,
    server_installation: ServerInstallation,
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> Iterator[Beebium]:
    """A machine on AUN station 80 whose name is a template of its station.

    The AUN transport is hermetic: an OS-chosen port, no discovery, no map
    file, and a private auto-station state file.
    """
    monkeypatch.setenv("BEEBIUM_AUN_AUTO_STATE_FILEPATH", str(tmp_path / "aun-auto-state.json"))
    with Beebium.launch(
        server=server_installation,
        mos_filepath=mos_filepath,
        extra_args=[
            "--machine-name",
            TEMPLATE,
            "--station",
            "80",
            "--aun",
            "port=0:discovery=off:map-file=none",
        ],
        startup_timeout=30.0,
    ) as bbc:
        yield bbc


def _wait_for_name(bbc: Beebium, name: str) -> str:
    """The machine's rendered name, once it is `name` or the timeout passes."""
    deadline = time.monotonic() + RENDER_TIMEOUT
    current = bbc.system.identity.name
    while current != name and time.monotonic() < deadline:
        time.sleep(0.2)
        current = bbc.system.identity.name
    return current


def test_a_launch_template_renders_the_station(station_80: Beebium) -> None:
    assert _wait_for_name(station_80, "Station 80") == "Station 80"
    assert station_80.system.identity.name_template == TEMPLATE


def test_placeholders_include_the_econet_station_with_its_value(station_80: Beebium) -> None:
    placeholders = {p.key: p for p in station_80.system.list_name_placeholders()}
    station = placeholders["econet-station"]
    assert station.value == "80"
    assert station.applicable
    assert station.insertion == "{econet-station}"
    assert station.label
    assert station.group
    assert station.description


def test_preview_reports_unknown_keys_without_renaming(station_80: Beebium) -> None:
    _wait_for_name(station_80, "Station 80")
    preview = station_80.system.preview_machine_name("Station {econet-station} {no-such-key}")
    assert preview.name == "Station 80 {no-such-key}"
    assert preview.report.unknown_keys == ("no-such-key",)
    assert preview.report.inapplicable_keys == ()
    assert preview.report.malformed == ()

    identity = station_80.system.identity
    assert identity.name == "Station 80"
    assert identity.name_template == TEMPLATE


def test_preview_reports_malformed_fragments(station_80: Beebium) -> None:
    preview = station_80.system.preview_machine_name("Station {econet-station")
    assert preview.report.malformed
    assert preview.name.startswith("Station ")


def test_set_applies_the_template_and_reports_unknown_keys(station_80: Beebium) -> None:
    change = station_80.system.set_machine_name("Net {econet-station} {no-such-key}")
    assert change.report.unknown_keys == ("no-such-key",)
    assert change.identity.name_template == "Net {econet-station} {no-such-key}"
    assert change.identity.name == "Net 80 {no-such-key}"

    identity = station_80.system.identity
    assert identity.name_template == "Net {econet-station} {no-such-key}"
    assert identity.name == "Net 80 {no-such-key}"


def test_a_plain_name_is_a_template_of_itself(station_80: Beebium) -> None:
    identity = station_80.system.identity
    identity.name = "Girton"
    assert identity.name == "Girton"
    assert identity.name_template == "Girton"
    assert station_80.system.identity.name_template == "Girton"


def test_setting_name_template_on_the_identity_renders_it(station_80: Beebium) -> None:
    identity = station_80.system.identity
    identity.name_template = "Girton {econet-station}"
    assert identity.name_template == "Girton {econet-station}"
    assert identity.name == "Girton 80"


def test_an_empty_template_is_refused(station_80: Beebium) -> None:
    with pytest.raises(grpc.RpcError) as excinfo:
        station_80.system.set_machine_name("")
    assert excinfo.value.code() == grpc.StatusCode.INVALID_ARGUMENT
    assert station_80.system.identity.name_template == TEMPLATE


def test_a_placeholder_without_econet_is_inapplicable_and_renders_empty(
    mos_filepath: Path,
    server_installation: ServerInstallation,
) -> None:
    with Beebium.launch(
        server=server_installation,
        mos_filepath=mos_filepath,
        startup_timeout=30.0,
    ) as bbc:
        placeholders = {p.key: p for p in bbc.system.list_name_placeholders()}
        station = placeholders["econet-station"]
        assert not station.applicable
        assert station.value == ""

        preview = bbc.system.preview_machine_name("Station [{econet-station}]")
        assert preview.name == "Station []"
        assert preview.report.inapplicable_keys == ("econet-station",)
        assert preview.report.unknown_keys == ()
